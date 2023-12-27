// This code is licensed under the GPLv3. You can find its text here:
// https://www.gnu.org/licenses/gpl-3.0.en.html */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sched.h>
#include <seccomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/capability.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <linux/capability.h>
#include <linux/limits.h>

struct ctn_config {
    int argc;
    uid_t uid;
    int fd;
    char *hostname;
    char **argv;
    char *mount_dir;
};

int setup_mounts(struct ctn_config *config) {
    fprintf(stderr, "=> remounting everything with MS_PRIVATE...");
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        fprintf(stderr, "failed! %m\n");
        return -1;
    }
    fprintf(stderr, "remounted.\n");

    fprintf(stderr, "=> making a temp directory and a bind mount there...");
    char mount_dir[] = "/tmp/tmp.XXXXXX";
    if (!mkdtemp(mount_dir)) {
        fprintf(stderr, "failed making a directory!\n");
        return -1;
    }
    fprintf(stderr, "\n=> temp directory %s\n", mount_dir);

    if (mount(config->mount_dir, mount_dir, NULL, MS_BIND | MS_PRIVATE, NULL)) {
        fprintf(stderr, "bind mount failed!\n");
        return -1;
    }

    char inner_mount_dir[] = "/tmp/tmp.XXXXXX/oldroot.XXXXXX";
    memcpy(inner_mount_dir, mount_dir, sizeof(mount_dir) - 1);
    if (!mkdtemp(inner_mount_dir)) {
        fprintf(stderr, "failed making the inner directory!\n");
        return -1;
    }
    fprintf(stderr, "done.\n");

    fprintf(stderr, "=> pivoting root...");
    if (syscall(SYS_pivot_root, mount_dir, inner_mount_dir)) {
        fprintf(stderr, "failed!\n");
        return -1;
    }
    fprintf(stderr, "done.\n");

    char *old_root_dir = basename(inner_mount_dir);
    char old_root[sizeof(inner_mount_dir) + 1] = { "/" };
    strcpy(&old_root[1], old_root_dir);

    fprintf(stderr, "=> unmounting %s...", old_root);
    if (chdir("/")) {
        fprintf(stderr, "chdir failed! %m\n");
        return -1;
    }
    if (umount2(old_root, MNT_DETACH)) {
        fprintf(stderr, "umount failed! %m\n");
        return -1;
    }
    if (rmdir(old_root)) {
        fprintf(stderr, "rmdir failed! %m\n");
        return -1;
    }
    fprintf(stderr, "done.\n");
    return 0;
}

int setup_userns(struct ctn_config *config) {
    fprintf(stderr, "=> trying a user namespace...");
    int has_userns = !unshare(CLONE_NEWUSER);
    if (write(config->fd, &has_userns, sizeof(has_userns)) != sizeof(has_userns)) {
        fprintf(stderr, "couldn't write: %m\n");
        return -1;
    }
    int result = 0;
    if (read(config->fd, &result, sizeof(result)) != sizeof(result)) {
        fprintf(stderr, "couldn't read: %m\n");
        return -1;
    }
    if (result) return -1;
    if (has_userns) {
        fprintf(stderr, "done.\n");
    } else {
        fprintf(stderr, "unsupported? continuing.\n");
    }

    fprintf(stderr, "=> switching to uid %d / gid %d...", config->uid, config->uid);
    if (setgroups(1, & (gid_t) { config->uid })) {
        fprintf(stderr, "setgroups failed: %m\n");
        return -1;
    }
    if (setresgid(config->uid, config->uid, config->uid)) {
        fprintf(stderr, "setresgid failed: %m\n");
        return -1;
    }
    if (setresuid(config->uid, config->uid, config->uid)) {
        fprintf(stderr, "setresuid failed: %m\n");
        return -1;
    }
    fprintf(stderr, "done.\n");
    return 0;
}

struct cgrp_control {
    char control[256];
    struct cgrp_setting {
        char name[256];
        char value[256];
    } **settings;
};
struct cgrp_setting add_to_tasks = {
    .name = "tasks",
    .value = "0"
};

struct cgrp_control *cgrps[] = {
    & (struct cgrp_control) {
        .control = "memory",
        .settings = (struct cgrp_setting *[]) {
            & (struct cgrp_setting) {
                .name = "memory.limit_in_bytes",
                .value = "1073741824"
            },
            /* & (struct cgrp_setting) { */
            /* 	.name = "memory.kmem.limit_in_bytes", */
            /* 	.value = "1073741824" */
            /* }, */
            &add_to_tasks,
            NULL
        }
    },
    & (struct cgrp_control) {
        .control = "cpu",
        .settings = (struct cgrp_setting *[]) {
            & (struct cgrp_setting) {
                .name = "cpu.shares",
                .value = "256" // CPU shares
            },
            &add_to_tasks,
            NULL
        }
    },
    & (struct cgrp_control) {
        .control = "pids",
        .settings = (struct cgrp_setting *[]) {
            & (struct cgrp_setting) {
                .name = "pids.max",
                .value = "64"
            },
            &add_to_tasks,
            NULL
        }
    },
    /* & (struct cgrp_control) { */
    /* 	.control = "blkio", */
    /* 	.settings = (struct cgrp_setting *[]) { */
    /* 		& (struct cgrp_setting) { */
    /* 			.name = "blkio.weight", // not found in kernel 5.10+, 6.1+ */
    /* 			.value = "10" */
    /* 		}, */
    /* 		&add_to_tasks, */
    /* 		NULL */
    /* 	} */
    /* }, */
    NULL
};

int setup_cgroups(struct ctn_config *config) {
    fprintf(stderr, "=> setting cgroups...");
    for (struct cgrp_control **cgrp = cgrps; *cgrp; cgrp++) {
        char dir[PATH_MAX] = {0};
        fprintf(stderr, "%s...", (*cgrp)->control);
        if (snprintf(dir, sizeof(dir), "/sys/fs/cgroup/%s/%s",
                    (*cgrp)->control, config->hostname) == -1) {
            return -1;
        }
        if (mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR)) {
            fprintf(stderr, "mkdir %s failed: %m\n", dir);
            return -1;
        }
        for (struct cgrp_setting **setting = (*cgrp)->settings; *setting; setting++) {
            char path[PATH_MAX] = {0};
            int fd = 0;
            if (snprintf(path, sizeof(path), "%s/%s", dir,
                        (*setting)->name) == -1) {
                fprintf(stderr, "snprintf failed: %m\n");
                return -1;
            }
            if ((fd = open(path, O_WRONLY)) == -1) {
                fprintf(stderr, "opening %s failed: %m\n", path);
                return -1;
            }
            if (write(fd, (*setting)->value, strlen((*setting)->value)) == -1) {
                fprintf(stderr, "writing to %s failed: %m\n", path);
                close(fd);
                return -1;
            }
            close(fd);
        }
    }
    fprintf(stderr, "done.\n");
    fprintf(stderr, "=> setting rlimit...");
    if (setrlimit(RLIMIT_NOFILE, & (struct rlimit) {.rlim_max = 64, .rlim_cur = 64,})) {
        fprintf(stderr, "failed: %m\n");
        return 1;
    }
    fprintf(stderr, "done.\n");
    return 0;
}

int cleanup_cgroups(struct ctn_config *config) {
    fprintf(stderr, "=> cleaning cgroups...");
    for (struct cgrp_control **cgrp = cgrps; *cgrp; cgrp++) {
        char dir[PATH_MAX] = {0};
        char task[PATH_MAX] = {0};
        int task_fd = 0;
        if (snprintf(dir, sizeof(dir), "/sys/fs/cgroup/%s/%s",
                    (*cgrp)->control, config->hostname) == -1
                || snprintf(task, sizeof(task), "/sys/fs/cgroup/%s/tasks",
                    (*cgrp)->control) == -1) {
            fprintf(stderr, "snprintf failed: %m\n");
            return -1;
        }
        if ((task_fd = open(task, O_WRONLY)) == -1) {
            fprintf(stderr, "opening %s failed: %m\n", task);
            return -1;
        }
        if (write(task_fd, "0", 2) == -1) {
            fprintf(stderr, "writing to %s failed: %m\n", task);
            close(task_fd);
            return -1;
        }
        close(task_fd);
        if (rmdir(dir)) {
            fprintf(stderr, "rmdir %s failed: %m", dir);
            return -1;
        }
    }
    fprintf(stderr, "done.\n");
    return 0;
}

int setup_capabilities() {
    fprintf(stderr, "=> dropping capabilities...");
    int drop_caps[] = {
        CAP_AUDIT_CONTROL,
        CAP_AUDIT_READ,
        CAP_AUDIT_WRITE,
        CAP_BLOCK_SUSPEND,
        CAP_DAC_READ_SEARCH,
        CAP_FSETID,
        CAP_IPC_LOCK,
        CAP_MAC_ADMIN,
        CAP_MAC_OVERRIDE,
        CAP_MKNOD,
        CAP_SETFCAP,
        CAP_SYSLOG,
        CAP_SYS_ADMIN,
        CAP_SYS_BOOT,
        CAP_SYS_MODULE,
        CAP_SYS_NICE,
        CAP_SYS_RAWIO,
        CAP_SYS_RESOURCE,
        CAP_SYS_TIME,
        CAP_WAKE_ALARM
    };
    size_t num_caps = sizeof(drop_caps) / sizeof(*drop_caps);
    fprintf(stderr, "bounding...");
    for (size_t i = 0; i < num_caps; i++) {
        if (prctl(PR_CAPBSET_DROP, drop_caps[i], 0, 0, 0)) {
            fprintf(stderr, "prctl failed: %m\n");
            return 1;
        }
    }
    fprintf(stderr, "inheritable...");
    cap_t caps = NULL;
    if (!(caps = cap_get_proc())
            || cap_set_flag(caps, CAP_INHERITABLE, num_caps, drop_caps, CAP_CLEAR)
            || cap_set_proc(caps)) {
        fprintf(stderr, "failed: %m\n");
        if (caps) cap_free(caps);
        return 1;
    }
    cap_free(caps);
    fprintf(stderr, "done.\n");
    return 0;
}

#define SCMP_FAIL SCMP_ACT_ERRNO(EPERM)

int setup_seccomp() {
    scmp_filter_ctx ctx = NULL;
    fprintf(stderr, "=> filtering syscalls...");
    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(chmod), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(chmod), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(fchmod), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(fchmod), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(fchmodat), 1, SCMP_A2(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(fchmodat), 1, SCMP_A2(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(unshare), 1, SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(clone), 1, SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(ioctl), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, TIOCSTI, TIOCSTI))
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(keyctl), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(add_key), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(request_key), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(ptrace), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(mbind), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(migrate_pages), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(move_pages), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(set_mempolicy), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(userfaultfd), 0)
            || seccomp_rule_add(ctx, SCMP_FAIL, SCMP_SYS(perf_event_open), 0)
            || seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 0)
            || seccomp_load(ctx)) {
                if (ctx) seccomp_release(ctx);
                fprintf(stderr, "failed: %m\n");
                return 1;
            }
    seccomp_release(ctx);
    fprintf(stderr, "done.\n");
    return 0;
}

int child(void *arg) {
    struct ctn_config *config = arg;
    if (sethostname(config->hostname, strlen(config->hostname))
            || setup_mounts(config)
            || setup_userns(config)
            || setup_capabilities()
            || setup_seccomp()) {
        close(config->fd);
        return -1;
    }

    if (close(config->fd)) {
        fprintf(stderr, "close failed: %m\n");
        return -1;
    }

    if (execve(config->argv[0], config->argv, NULL)) {
        fprintf(stderr, "execve failed! %m.\n");
        return -1;
    }

    return 0;
}

int gen_hostname(char *buff, size_t len) {
    static const char *suits[] = { "swords", "wands", "pentacles", "cups" };
    static const char *minor[] = {
        "ace", "two", "three", "four", "five", "six", "seven", "eight",
        "nine", "ten", "page", "knight", "queen", "king"
    };
    static const char *major[] = {
        "fool", "magician", "high-priestess", "empress", "emperor",
        "hierophant", "lovers", "chariot", "strength", "hermit",
        "wheel", "justice", "hanged-man", "death", "temperance",
        "devil", "tower", "star", "moon", "sun", "judgment", "world"
    };
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    size_t ix = now.tv_nsec % 78;
    if (ix < sizeof(major) / sizeof(*major)) {
        snprintf(buff, len, "%05lx-%s", now.tv_sec, major[ix]);
    } else {
        ix -= sizeof(major) / sizeof(*major);
        snprintf(buff, len,
                "%05lxc-%s-of-%s",
                now.tv_sec,
                minor[ix % (sizeof(minor) / sizeof(*minor))],
                suits[ix / (sizeof(minor) / sizeof(*minor))]);
    }
    return 0;
}

#define USERNS_OFFSET 10000
#define USERNS_COUNT 2000

int handle_child_uid_map (pid_t child_pid, int fd) {
    int uid_map = 0;
    int has_userns = -1;
    if (read(fd, &has_userns, sizeof(has_userns)) != sizeof(has_userns)) {
        fprintf(stderr, "couldn't read from child!\n");
        return -1;
    }
    if (has_userns) {
        char path[PATH_MAX] = {0};
        for (char **file = (char *[]) { "uid_map", "gid_map", 0 }; *file; file++) {
            if (snprintf(path, sizeof(path), "/proc/%d/%s", child_pid, *file)
                    > sizeof(path)) {
                fprintf(stderr, "snprintf too big? %m\n");
                return -1;
            }
            fprintf(stderr, "writing %s...", path);
            if ((uid_map = open(path, O_WRONLY)) == -1) {
                fprintf(stderr, "open failed: %m\n");
                return -1;
            }
            if (dprintf(uid_map, "0 %d %d\n", USERNS_OFFSET, USERNS_COUNT) == -1) {
                fprintf(stderr, "dprintf failed: %m\n");
                close(uid_map);
                return -1;
            }
            close(uid_map);
        }
    }
    if (write(fd, & (int) { 0 }, sizeof(int)) != sizeof(int)) {
        fprintf(stderr, "couldn't write: %m\n");
        return -1;
    }
    return 0;
}

int main (int argc, char **argv) {
    struct ctn_config config = {0};
    int err = 0;
    int option = 0;
    int sockets[2] = {0};
    pid_t child_pid = 0;
    int last_optind = 0;

    //////////////////////////////////////////////////////////////////////////
    // Step 1: parse CLI parameters, validate Linux kernel/cpu/..., generate hostname for container
    //////////////////////////////////////////////////////////////////////////
    while ((option = getopt(argc, argv, "c:m:u:"))) {
        switch (option) {
            case 'c':
                config.argc = argc - last_optind - 1;
                config.argv = &argv[argc - config.argc];
                goto finish_options;
            case 'm':
                config.mount_dir = optarg;
                break;
            case 'u':
                if (sscanf(optarg, "%d", &config.uid) != 1) {
                    fprintf(stderr, "invalid uid: %s\n", optarg);
                    goto usage;
                }
                break;
            default:
                goto usage;
        }
        last_optind = optind;
    }
finish_options:
    if (!config.argc) goto usage;
    if (!config.mount_dir) goto usage;

    fprintf(stderr, "=> validating Linux version...");
    struct utsname host = {0};
    if (uname(&host)) {
        fprintf(stderr, "failed: %m\n");
        goto cleanup;
    }

    int major = -1;
    int minor = -1;
    if (sscanf(host.release, "%u.%u.", &major, &minor) != 2) {
        fprintf(stderr, "weird release format: %s\n", host.release);
        goto cleanup;
    }

    if (major != 6 || (minor != 0 && minor != 1)) {
        fprintf(stderr, "expected 6.x: %s\n", host.release);
        goto cleanup;
    }

    if (strcmp("x86_64", host.machine)) {
        fprintf(stderr, "expected x86_64: %s\n", host.machine);
        goto cleanup;
    }
    fprintf(stderr, "%s on %s.\n", host.release, host.machine);

    fprintf(stderr, "=> generating hostname for container ... ");
    char ctn_hostname[256] = {0};
    if (gen_hostname(ctn_hostname, sizeof(ctn_hostname)))
        goto error;
    config.hostname = ctn_hostname;
    fprintf(stderr, "%s done\n", ctn_hostname);

    //////////////////////////////////////////////////////////////////////////
    // Step 2: setup a socket pair for container sending messages to the parent process
    //////////////////////////////////////////////////////////////////////////
    if (socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, sockets)) {
        fprintf(stderr, "socketpair failed: %m\n");
        goto error;
    }
    if (fcntl(sockets[0], F_SETFD, FD_CLOEXEC)) {
        fprintf(stderr, "fcntl failed: %m\n");
        goto error;
    }
    config.fd = sockets[1];

    ///////////////////////////////////////////////////////////////////////
    // Step 3: allocate stack space for `execve()`
    ///////////////////////////////////////////////////////////////////////
#define STACK_SIZE (1024 * 1024)
    char *stack = 0;
    if (!(stack = malloc(STACK_SIZE))) {
        fprintf(stderr, "=> malloc failed, out of memory?\n");
        goto error;
    }

    ///////////////////////////////////////////////////////////////////////
    // Step 4: setup cgroup for the container for resource isolation
    ///////////////////////////////////////////////////////////////////////
    if (setup_cgroups(&config)) {
        err = 1;
        goto clear_resources;
    }

    ///////////////////////////////////////////////////////////////////////
    // Step 5: launch container
    ///////////////////////////////////////////////////////////////////////
    int flags = CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWUTS;
    if ((child_pid = clone(child, stack + STACK_SIZE, flags | SIGCHLD, &config)) == -1) {
        fprintf(stderr, "=> clone failed! %m\n");
        err = 1;
        goto clear_resources;
    }

    ///////////////////////////////////////////////////////////////////////
    // Step 6: error handling and cleanup
    ///////////////////////////////////////////////////////////////////////
    close(sockets[1]);
    sockets[1] = 0;
    close(sockets[1]);
    sockets[1] = 0;
    if (handle_child_uid_map(child_pid, sockets[0])) {
        err = 1;
        goto kill_and_finish_child;
    }

    goto finish_child;
kill_and_finish_child:
    if (child_pid) kill(child_pid, SIGKILL);
finish_child:;
             int child_status = 0;
             waitpid(child_pid, &child_status, 0);
             err |= WEXITSTATUS(child_status);
clear_resources:
             cleanup_cgroups(&config);
             free(stack);

             goto cleanup;
usage:
             fprintf(stderr, "Usage: %s -u 0 -m ~/busybox-rootfs-1.36/ -c /bin/whoami\n", argv[0]);
error:
             err = 1;
cleanup:
             if (sockets[0]) close(sockets[0]);
             if (sockets[1]) close(sockets[1]);
             return err;
}
