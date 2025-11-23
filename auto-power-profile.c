#define _GNU_SOURCE

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include "log.h"

static void
do_expect(const char *const file, int line, bool condition) {
    if (!condition) {
        do_log("ERROR", file, line, "Assertion failed");
        abort();
    }
}

/// Mainly used to check for out of memory conditions. We will not handle those
/// in this program. Like assert but not affected by _NDEBUG.
#define expect(_cond) do_expect(__FILE__, __LINE__, (_cond))

static int
create_netlink(void) {
    const int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        error("Failed to create netlink socket: %s", strerror(errno));
        goto failure;
    }

    const struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_groups = 1,
    };

    const int ret = bind(fd, (const struct sockaddr *)&addr, sizeof addr);
    if (ret < 0) {
        error("Failed to bind netlink socket: %s", strerror(errno));
        goto failure;
    }

    return fd;

failure:
    if (fd >= 0) close(fd);
    return -1;
}

enum event {
    EVENT_ERROR = -1,
    EVENT_NONE = 0,
    EVENT_MAINS_ONLINE,
    EVENT_MAINS_OFFLINE,
    EVENT_BATTERY_LOW,
    EVENT_BATTERY_NOT_LOW,
};

struct uevent {
    const char *devtype;
    const char *power_supply_online;
    const char *power_supply_type;
    const char *power_supply_capacity;
    const char *power_supply_scope;
};

/// Check key and return value if it matches.
static inline const char *
do_iskey(const char *restrict key, size_t keylen, const char *line,
         size_t line_len) {
    assert(key);
    assert(line);

    if (line_len >= keylen && memcmp(key, line, keylen) == 0) {
        return line + keylen;
    } else {
        return NULL;
    }
}

#define iskey(_key, _line, _line_len)                                          \
    do_iskey(_key "=", sizeof _key, (_line), (_line_len))

static void
uevent_parse_line(struct uevent *e, const char *restrict line,
                  size_t line_len) {
    assert(e);
    assert(line);

    const char *value = NULL;
    if ((value = iskey("DEVTYPE", line, line_len))) {
        e->devtype = value;
    } else if ((value = iskey("POWER_SUPPLY_ONLINE", line, line_len))) {
        e->power_supply_online = value;
    } else if ((value = iskey("POWER_SUPPLY_TYPE", line, line_len))) {
        e->power_supply_type = value;
    } else if ((value = iskey("POWER_SUPPLY_CAPACITY", line, line_len))) {
        e->power_supply_capacity = value;
    } else if ((value = iskey("POWER_SUPPLY_SCOPE", line, line_len))) {
        e->power_supply_scope = value;
    }
}

static enum event
uevent_to_event(const struct uevent *e) {
    if (!e->devtype || strcmp(e->devtype, "power_supply") != 0)
        return EVENT_NONE;

    if (!e->power_supply_type) return EVENT_NONE;
    if (strcmp(e->power_supply_type, "Mains") == 0) {
        if (!e->power_supply_online) {
            error("Received invalid message");
            return EVENT_ERROR;
        }
        if (strcmp(e->power_supply_online, "1") == 0) return EVENT_MAINS_ONLINE;

        if (strcmp(e->power_supply_online, "0") == 0)
            return EVENT_MAINS_OFFLINE;

        error("Received invalid Value for POWER_SUPPLY_ONLINE: %s\n",
              e->power_supply_online);
        return EVENT_ERROR;
    }

    if (strcmp(e->power_supply_type, "Battery") == 0) {
        // Skip batteries of connected devices etc.
        if (e->power_supply_scope &&
            strcmp(e->power_supply_scope, "Sytem") != 0)
            return EVENT_NONE;

        // Skip batteries that don't specify a capacity.
        if (!e->power_supply_capacity) {
            return EVENT_NONE;
        }

        char *end;
        const long capacity = strtol(e->power_supply_capacity, &end, 10);

        // Check if parsed capacity is valid.
        assert(end);
        if (e->power_supply_capacity[0] == '\0' || *end != '\0') {
            error("Received invalid message");
            return EVENT_ERROR;
        }

        if (capacity <= 20) {
            return EVENT_BATTERY_LOW;
        } else {
            return EVENT_BATTERY_NOT_LOW;
        }
    }

    return EVENT_NONE;
}

static enum event
parse_uevent(const char *buffer, size_t len) {
    assert(buffer);

    struct uevent uevent = {0};

    size_t offset = 0;
    while (offset < len) {
        const char *const line = buffer + offset;

        const size_t line_len = strnlen(line, len - offset);

        uevent_parse_line(&uevent, line, line_len);

        offset += line_len + 1;
    }

    assert(offset == len);

    return uevent_to_event(&uevent);
}

static enum event
parse_netlink_uevent(const char *buffer, size_t len) {
    assert(buffer);

    // Skip title line
    const size_t offset = strnlen(buffer, len) + 1;
    assert(offset < len);

    return parse_uevent(buffer + offset, len - offset);
}

// Modifies buffer.
static enum event
parse_initial_uevent(char *buffer, size_t len) {
    assert(buffer);

    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == '\n') buffer[i] = '\0';
    }

    return parse_uevent(buffer, len);
}

struct state {
    bool mains_online;
    bool battery_low;
};

static bool
set_power_profile(DBusConnection *connection, const char *profile) {
    assert(connection);
    assert(profile);

    bool success = true;

    DBusError err = DBUS_ERROR_INIT;
    DBusMessage *request = NULL, *response = NULL;

    request = dbus_message_new_method_call(
        "net.hadess.PowerProfiles", "/net/hadess/PowerProfiles",
        "org.freedesktop.DBus.Properties", "Set");
    expect(request);

    const char *const interface = "net.hadess.PowerProfiles";
    const char *const property = "ActiveProfile";

    expect(dbus_message_append_args(request, DBUS_TYPE_STRING, &interface,
                                    DBUS_TYPE_STRING, &property,
                                    DBUS_TYPE_INVALID));

    DBusMessageIter variant_iter, args;
    dbus_message_iter_init_append(request, &args);

    expect(dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "s",
                                            &variant_iter));

    expect(dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING,
                                          &profile));

    expect(dbus_message_iter_close_container(&args, &variant_iter));

    response = dbus_connection_send_with_reply_and_block(connection, request,
                                                         -1, &err);

    if (response == NULL) {
        error("DBus method call failed: %s", err.message);
        success = false;
        goto cleanup;
    }

cleanup:
    if (response) dbus_message_unref(response);
    if (request) dbus_message_unref(request);
    dbus_error_free(&err);

    return success;
}

static bool
get_initial_state(struct state *state, DBusConnection *connection) {
    assert(state);
    assert(connection);

    enum { BUF_SIZE = 4096 };

    const char dirpath[] = "/sys/class/power_supply/";

    DIR *const dir = opendir(dirpath);
    if (dir == NULL) {
        error("Could not open %s: %s", dirpath, strerror(errno));
        return false;
    }

    const struct dirent *dirent;
    while ((dirent = readdir(dir))) {
        if (strcmp(dirent->d_name, "..") == 0 ||
            strcmp(dirent->d_name, ".") == 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s%s/uevent", dirpath, dirent->d_name);

        const int fd = open(path, O_RDONLY);
        if (fd < 0) goto cleanup;

        char buffer[BUF_SIZE];
        const ssize_t len = read(fd, buffer, sizeof buffer - 1);
        if (len < 0) goto cleanup;
        buffer[len] = '\0';

        const enum event event = parse_initial_uevent(buffer, len);
        switch (event) {
        case EVENT_ERROR:
        case EVENT_NONE:
            continue;
        case EVENT_MAINS_ONLINE:
            state->mains_online = true;
            break;
        case EVENT_MAINS_OFFLINE:
            state->mains_online = false;
            break;
        case EVENT_BATTERY_LOW:
            state->battery_low = true;
            break;
        case EVENT_BATTERY_NOT_LOW:
            state->battery_low = false;
            break;
        }

    cleanup:
        close(fd);
    }

    closedir(dir);

    const char *profile;
    if (state->mains_online) {
        profile = "performance";
    } else if (state->battery_low) {
        profile = "power-saver";
    } else {
        profile = "balanced";
    }

    const bool success = set_power_profile(connection, profile);
    if (!success) error("Failed to set power profile to '%s'", profile);
    return success;
}

static void
handle_event(struct state *state, DBusConnection *connection,
             enum event event) {
    assert(state);
    assert(connection);

    const char *power_profile = NULL;
    switch (event) {
    case EVENT_ERROR:
        error("Received invalid event");
        break;
    case EVENT_NONE:
        break;
    case EVENT_MAINS_ONLINE:
        if (!state->mains_online) {
            state->mains_online = true;
            power_profile = "performance";
        }
        break;
    case EVENT_MAINS_OFFLINE:
        if (state->mains_online) {
            state->mains_online = false;
            power_profile = "balanced";
        }
        break;
    case EVENT_BATTERY_LOW:
        if (!state->battery_low) {
            state->battery_low = true;
            power_profile = "power-saver";
        }
        break;
    case EVENT_BATTERY_NOT_LOW:
        if (state->battery_low) {
            state->battery_low = false;

            if (!state->mains_online) power_profile = "balanced";
        }
        break;
    }

    if (power_profile) {
        const bool success = set_power_profile(connection, power_profile);
        if (!success) error("Failed to set power mode to '%s'", power_profile);
    }
}

int
main(void) {
    enum { BUFSIZE = 2048 };

    int status = 0;
    int netlink = -1;
    DBusConnection *connection = NULL;
    DBusError dbus_error = DBUS_ERROR_INIT;

    netlink = create_netlink();
    if (netlink < 0) {
        status = 1;
        error("Failed to create netlink");
        goto cleanup;
    }

    connection = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_error);
    if (connection == NULL) {
        status = 1;
        error("Failed to setup DBus connection: %s", dbus_error.message);
        goto cleanup;
    }

    struct state state;
    const bool success = get_initial_state(&state, connection);
    if (!success) {
        error("Failed to get initial state");
        status = 1;
        goto cleanup;
    }

    while (true) {
        // Always null terminated!
        char buf[BUFSIZE] = {0};
        ssize_t len = recv(netlink, buf, sizeof buf - 1, 0);

        if (len < 0) {
            if (errno == EINTR) continue;

            error("Failed to recv on netlink socket: %s", strerror(errno));
            status = 1;
            goto cleanup;
        }

        const enum event event = parse_netlink_uevent(buf, (size_t)len);
        if (event == EVENT_ERROR) {
            error("Failed to handle uevent %.*s", (int)len, buf);
            continue;
        }

        handle_event(&state, connection, event);
    }

cleanup:
    if (connection) dbus_connection_unref(connection);
    if (netlink != -1) close(netlink);

    return status;
}
