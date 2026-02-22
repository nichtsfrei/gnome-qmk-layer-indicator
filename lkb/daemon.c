#include <assert.h>
#include <dbus/dbus.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define HIDRAW_DIR "/dev"

jmp_buf jmp_clean_up;

struct LayeredKeyboard {
  int fd;
  char *path;
  char layer;
};

struct MaybeLKB {
  int result;
  struct LayeredKeyboard kb;
};

static DBusConnection *global_conn = NULL;
static pthread_once_t conn_once = PTHREAD_ONCE_INIT;

static void init_dbus_connection(void) {
  DBusError err;
  dbus_error_init(&err);

  global_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (global_conn == NULL) {
    fprintf(stderr, "DBus connection error: %s\n",
            err.message ? err.message : "unknown");
    goto exit;
  }

  dbus_connection_flush(global_conn);
exit:
  dbus_error_free(&err);
}

static void close_global_dbus(void) {
  if (global_conn) {
    dbus_connection_unref(global_conn);
    global_conn = NULL;
  }
}

static void emit_layer_signal(struct LayeredKeyboard *kb) {
  pthread_once(&conn_once, init_dbus_connection);
  if (!global_conn)
    return;
  DBusError err;
  DBusMessage *msg;

  char buf[16];
  // As I just one qmk keyboard at a time, I think it is fine, otherwise extend
  // with path
  snprintf(buf, sizeof(buf), "L: %u", kb->layer);
  char *message = buf;

  dbus_error_init(&err);

  msg = dbus_message_new_signal("/de/nichtsfrei/PanelMessage", // object path
                                "de.nichtsfrei.PanelMessage",  // interface
                                "Message"                      // signal name
  );

  if (!msg) {
    fprintf(stderr, "Failed to create DBus message\n");
    goto flush_connection;
  }

  // urgh
  if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &message,
                                DBUS_TYPE_INVALID)) {
    fprintf(stderr, "Failed to append DBus args\n");
  } else if (!dbus_connection_send(global_conn, msg, NULL)) {
    fprintf(stderr, "Failed to send DBus message\n");
  }
  dbus_message_unref(msg);
flush_connection:
  dbus_connection_flush(global_conn);
  dbus_error_free(&err);
}

void *probe_device(void *out) {
  assert(out != NULL);
  struct MaybeLKB *result = out;
  unsigned char data[32] = {0};
  data[0] = 'L';
  struct timeval timeout = {1, 0};
  if (result->kb.path == NULL) {
    result->result = -2;
    return NULL;
  }

  // indicates issues that have set errno
  result->result = -1;
  result->kb.fd = open(result->kb.path, O_RDWR);
  if (result->kb.fd == -1) {
    goto end;
  }
  int res = write(result->kb.fd, data, sizeof(data));
  if (res == -1) {
    goto end;
  }

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(result->kb.fd, &read_fds);
  res = select(result->kb.fd + 1, &read_fds, NULL, NULL, &timeout);
  if (res <= -1) {
    goto end;
  } else if (res == 0) {
    result->result = 0;
    goto end;
  }

  printf("sizeof data: %zu\n", sizeof(data));
  res = read(result->kb.fd, data, sizeof(data));
  if (res > 0 && data[0] == 'L' && data[1] == 1) {
    result->result = 1;
    result->kb.layer = data[31];
    emit_layer_signal(&result->kb);
  } else {
    result->result = 0;
  }
end:
  printf("probed %s\tresult: %3i\n", result->kb.path, result->result);
  return NULL;
}

struct LayeredKeyboards {
  struct LayeredKeyboard *kbs;
  size_t len;
};

void free_layered_keyboards_content(struct LayeredKeyboards *kbs) {
  if (kbs == NULL)
    return;
  if (kbs->kbs == NULL)
    return;
  free(kbs->kbs);
}

struct LayeredKeyboards probe_devices() {
  struct dirent *entry;
  DIR *dir = opendir(HIDRAW_DIR);
  size_t hd = strlen(HIDRAW_DIR);
  size_t devices = 0, device_cap = 20, name_len;
  size_t layerd_devices = 0;
  char **device_paths = calloc(device_cap, sizeof(*device_paths));
  struct LayeredKeyboards result = {NULL, 0};

  if (dir == NULL) {
    perror("Failed to open /dev directory");
    free(device_paths);
    return result;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (strncmp(entry->d_name, "hidraw", 6) == 0) {
      if (devices == device_cap) {
        device_cap *= 2;
        device_paths =
            realloc(device_paths, device_cap * sizeof(*device_paths));
      }
      name_len = hd + strlen(entry->d_name) + 2;
      device_paths[devices] = calloc(1, name_len);

      snprintf(device_paths[devices], name_len, "%s/%s", HIDRAW_DIR,
               entry->d_name);
      devices += 1;
    }
  }
  closedir(dir);
  struct MaybeLKB layered[devices];
  pthread_t threads[devices];

  for (size_t i = 0; i < devices; ++i) {
    layered[i].kb.path = device_paths[i];
    device_paths[i] = NULL;
    pthread_create(&threads[i], NULL, probe_device, (void *)&layered[i]);
  }

  for (size_t i = 0; i < devices; ++i) {
    pthread_join(threads[i], NULL);
    if (layered[i].result == 1) {
      layerd_devices += 1;

    } else {
      free(layered[i].kb.path);
      layered[i].kb.path = NULL;
      close(layered[i].kb.fd);
    }
  }

  size_t ri = 0;
  result.len = layerd_devices;
  result.kbs = malloc(result.len * sizeof(*result.kbs));
  for (size_t i = 0; i < devices; ++i) {
    if (layered[i].result == 1) {
      result.kbs[ri] = layered[i].kb;
      layered[i].kb.path = NULL;
      ri += 1;
    }
  }
  free(device_paths);
  return result;
}

static int read_hid(struct LayeredKeyboard *kb) {
  char data[32];
  if (read(kb->fd, data, sizeof(data)) == -1) {
    perror("unable to read from hid device\n");
    return -1;
  }
  if (data[0] == 'L') {
    kb->layer = data[31];
    emit_layer_signal(kb);
    printf("%s\tlayer: %u\n", kb->path, kb->layer);
    return 1;
  } else {
    fprintf(stderr, "%s\tunknown message: %c\t size: %u\n", kb->path, data[0],
            data[1]);
    return 0;
  }
}

int listen_socket(const char *path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    return -1;
  struct sockaddr_un addr = {0};

  unlink(path);
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
    goto failure;
  }
  if (listen(fd, 5) == -1) {
    goto failure;
  }
  printf("listening on %s\n", path);
  return fd;
failure:
  close(fd);
  return fd;
}

pthread_rwlock_t rwlock;

struct LayeredKeyboards kbs = {0};

void close_unix_socket(void *arg) {
  int fd = *((int *)arg);
  close(fd);
}

void unlink_unix_socket(void *arg) {
  const char *path = arg;
  unlink(path);
}

void cleanup(int signum) { longjmp(jmp_clean_up, 1); }

void *unix_socket(void *arg) {
  const char *path = arg;
  char data[32] = {0};

  char answer[1024] = {0};
  char *header = "device\tlayer\n";
  size_t header_len = strlen(header);
  struct pollfd fds[1];

  pthread_cleanup_push(close_unix_socket, &fds[0]);
  pthread_cleanup_push(unlink_unix_socket, arg);
  fds[0].fd = listen_socket(path);
  if (fds[0].fd == -1) {
    perror("Failed to create socket");
    exit(1);
  }
  fds[0].events = POLLIN;

  while (1) {
    if (poll(fds, 1, -1) == -1) {
      perror("poll failed");
      exit(1);
    }
    if (fds[0].revents & POLLERR) {
      printf("Unix socket lost. Reinitilizing.\n");
      close(fds[0].fd);
      fds[0].fd = listen_socket(path);
      if (fds[0].fd == -1) {
        perror("Failed to create socket");
        exit(1);
      }
    }
    if (fds[0].revents & POLLIN) {
      int client_fd = accept(fds[0].fd, NULL, NULL);
      if (client_fd == -1) {
        perror("Failed to accept Unix socket connection");
        continue;
      }

      if (read(client_fd, data, sizeof(data)) == -1) {
        perror("Failed to read from Unix socket");
        close(client_fd);
        continue;
      }

      if (data[0] == 'L') {
        if (write(client_fd, header, header_len) == -1) {
          perror("Failed to send header to client");
        } else {

          pthread_rwlock_rdlock(&rwlock);
          for (size_t i = 0; i < kbs.len; ++i) {
            int size = snprintf(answer, 1024, "%s\t%u\n", kbs.kbs[i].path,
                                kbs.kbs[i].layer);
            if (write(client_fd, answer, size) == -1) {
              perror("Failed to send state to client");
            }
          }
          pthread_rwlock_unlock(&rwlock);
        }
      }
      close(client_fd);
    }
  }
  pthread_cleanup_pop(0);
  pthread_cleanup_pop(1);
}

int poll_kbs(struct pollfd *fds, unsigned char timeout) {

  int res;
  for (res = 1; res > 0;) {
    res = poll(fds, kbs.len, timeout * 1000);
    if (res == 0) {
      printf("timeout reached.\n");
    } else if (res == -1) {
      perror("poll failed");
    } else {
      pthread_rwlock_wrlock(&rwlock);
      for (size_t i = 0; i < kbs.len; ++i) {

        if (fds[i].revents & POLLERR) {
          printf("Device %s lost.\n", kbs.kbs[i].path);
          res = -2;
          break;
        }
        if (fds[i].revents & POLLIN && read_hid(&kbs.kbs[i]) == -1) {
          perror("unable to read from hid device");
          res = -2;
          break;
        }
      }
      pthread_rwlock_unlock(&rwlock);
    }
  }
  return res == 1 ? 0 : res;
}

int main(int argc, char *argv[]) {
  int res = -1;
  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

  if (runtime_dir == NULL) {
    fprintf(stderr, "XDG_RUNTIME_DIR missing.");
    exit(1);
  }
  const char *sock_name = "lkbd.sock";

  char socket_path[strlen(runtime_dir) + strlen(sock_name) + 2];
  sprintf(socket_path, "%s/%s", runtime_dir, sock_name);
  unsigned char timeout = 30;
  struct pollfd *fds = NULL;
  pthread_rwlock_init(&rwlock, NULL);
  pthread_t us_socks;

  if (signal(SIGINT, cleanup) == SIG_ERR) {
    perror("Error setting signal handler for SIGINT");
    exit(1);
  }

  if (signal(SIGTERM, cleanup) == SIG_ERR) {
    perror("Error setting signal handler for SIGTERM");
    exit(1);
  }

  pthread_create(&us_socks, NULL, unix_socket, (void *)socket_path);
  if (setjmp(jmp_clean_up) == 1)
    goto result;

  while (1) {

    pthread_rwlock_wrlock(&rwlock);
    free(fds);
    fds = NULL;

    kbs = probe_devices();
    pthread_rwlock_unlock(&rwlock);
    printf("found %zu devices. Timeout is at %u seconds.\n", kbs.len, timeout);

    if (kbs.len == 0) {
      sleep(timeout);
    } else {
      fds = malloc(sizeof(*fds) * kbs.len);
      for (size_t i = 0; i < kbs.len; ++i) {
        fds[i].fd = kbs.kbs[i].fd;
        fds[i].events = POLLIN;
      }
      while (1)
        if (poll_kbs(fds, timeout) == -2)
          break;
    }
    for (size_t i = 0; i < kbs.len; i++) {
      if (fds[i].fd != -1)
        close(fds[i].fd);
      free(kbs.kbs[i].path);
      kbs.kbs[i].path = NULL;
    }

    free_layered_keyboards_content(&kbs);
  }

result:
  pthread_cancel(us_socks);
  close_global_dbus();

  for (size_t i = 0; i < kbs.len; i++) {
    if (fds[i].fd != -1)
      close(fds[i].fd);
    free(kbs.kbs[i].path);
  }
  free(kbs.kbs);
  free(fds);
  pthread_join(us_socks, NULL);
  return res;
}
