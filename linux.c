#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define TREES 4
#define MAX_RECORDS 200

typedef struct {
  int basket, banana, phase, fallen, caught;
  int speed, running, finished, limit;
} GameState;

typedef struct {
  char date[20], name[40];
  int caught, fallen, speed, limit;
} Score;

static GameState game = {.basket = 1, .speed = 1, .limit = 25};
static char device[128] = "/dev/ttyACM0";
static char score_file[128] = "scores.txt";
static char player[40] = "Játékos";
/* Variables for editing the player name. */
static bool editing_name = false;
static char typed_name[40] = "";
static size_t typed_length = 0;

/* Start the game automatically with the -s option. */
static bool auto_start = false;
static int start_speed = 1, start_limit = 25;
static int serial_fd = -1;
static bool speed_given = false, limit_given = false;
static bool have_state = false, was_running = false, score_saved = false;

/* Interactive help and game configuration. */
static bool showing_help = false;
static char setting_mode = '\0';
static char setting_text[12] = "";
static size_t setting_length = 0;
static char feedback[128] = "";

/* Keep the warning visible during the game. */
static bool setting_warning = false;
static struct termios old_terminal;
static bool terminal_changed = false;
static volatile sig_atomic_t stopping = 0;

/* Ctrl+C requests exit; cleanup happens in main(), not in the handler. */
static void stop_handler(int signal_number) {
  (void)signal_number;
  stopping = 1;
}

/* Print help*/
static void help(const char *program) {
  printf("Használat: %s [-v sebesség] [-m banánok] "
         "[-n név] [-t eredménytábla] [-s] [-h]\n",
         program);

  puts("  -v 1..5  Játéksebesség");
  puts("  -m 25..99  Maximális banánszám");
  puts("  -n NÉV   Játékos neve");
  puts("  -t FÁJL  Eredménytábla (alapértelmezett: scores.txt)");
  puts("  -s       Játék közvetlen indítása");
  puts("  -h       Súgó");

  puts("Gombok: s indítás, b balra, j jobbra, x reset, q kilépés");
  puts("        n név, v sebesség, m banánlimit, +/- sebesség");
  puts("        t eredménytábla, h részletes súgó");
}

/* Parse the input completely; atoi() would silently accept invalid text. */
static bool parse_number(const char *text, int minimum, int maximum,
                         int *result) {
  char *end;
  long number;
  errno = 0;
  number = strtol(text, &end, 10);
  if (errno || end == text || *end != '\0' || number < minimum ||
      number > maximum)
    return false;
  *result = (int)number;
  return true;
}

static int open_serial(const char *port) {
  int fd = open(port, O_RDWR | O_NOCTTY);
  if (fd < 0)
    return -1;

  struct termios settings;
  if (tcgetattr(fd, &settings) != 0) {
    close(fd);
    return -1;
  }
  cfmakeraw(&settings);
  settings.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
  settings.c_cflag |= CS8 | CLOCAL | CREAD;
  settings.c_cc[VMIN] = 1;
  settings.c_cc[VTIME] = 0;
  cfsetispeed(&settings, B115200);
  cfsetospeed(&settings, B115200);
  if (tcsetattr(fd, TCSANOW, &settings) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/* Send a complete command; the firmware expects v3\n or m50\n. */
static bool send_command(const char *command) {
  size_t length = strlen(command), sent = 0;
  while (sent < length) {
    ssize_t n = write(serial_fd, command + sent, length - sent);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      perror("Soros port írása");
      stopping = 1;
      return false;
    }
    sent += (size_t)n;
  }
  return true;
}

/* Draw the board from the nine numbers received from the microcontroller. */
static void draw_game(void) {
  if (isatty(STDOUT_FILENO))
    printf("\033[2J\033[H");

  /* Keep the help page visible until the user presses a key. */
  if (showing_help) {
    puts("========= BANÁNFA - RÉSZLETES SÚGÓ =========");
    puts("A játék célja a négy fáról leeső banánok elkapása.");
    puts("A kosarat a fák alá kell mozgatni.");

    puts("\nJÁTÉKVEZÉRLÉS:");
    puts("s = játék indítása");
    puts("b = kosár balra, j = kosár jobbra");
    puts("A fejlesztőkártya PB0/PB1 gombjai is használhatók.");
    puts("x = reset, q = kilépés és leállítás");

    puts("\nBEÁLLÍTÁSOK:");
    puts("n = játékos nevének megadása");
    puts("v = sebesség megadása (1..5), majd Enter");
    puts("m = maximális banánszám (25..99), majd Enter");
    puts("+/- = sebesség növelése vagy csökkentése");
    puts("A beállítások csak játék előtt módosíthatók.");

    puts("\nEREDMÉNYEK:");
    puts("t = eredménytábla");
    puts("A befejezett játék eredménye fájlba kerül.");

    puts("\nVisszatéréshez nyomj meg egy billentyűt.");
    fflush(stdout);
    return;
  }

  printf("\n========= BANÁNFA =========\n");
  printf("Játékos: %s | Sebesség: %d/5 | Banánlimit: %d\n", player, game.speed,
         game.limit);
  printf("Leesett: %d | Elkapott: %d | Elhibázott: %d\n", game.fallen,
         game.caught, game.fallen - game.caught);
  puts("\n     FA1      FA2      FA3      FA4");
  puts("    /|\\      /|\\      /|\\      /|\\");
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < TREES; ++col)
      printf("    %-7s", game.running && game.banana == col && game.phase == row
                             ? "(o)"
                             : "|");
    putchar('\n');
  }
  for (int col = 0; col < TREES; ++col)
    printf("    %-7s", game.basket == col ? "[___]" : "");
  printf("\n\nÁllapot: %s\n", game.running    ? "Játék folyamatban"
                              : game.finished ? "Játék vége"
                                              : "Indításra kész");
  puts("s=start | b=bal | j=jobb | x=reset | q=kilépés | t=eredmények");
  puts("Indítás előtt: n=név | v=sebesség | m=banánlimit | +/- sebesség | "
       "h=súgó");
  /* Display the warning on every screen refresh. */
  if (setting_warning) {
    puts("\nFIGYELEM: A beállításokat csak játék előtt módosíthatod!");
  }

  if (editing_name) {
    printf("\nÚj játékosnév: %s_\n", typed_name);
    puts("Enter = mentés | Backspace = törlés | Esc = mégse");
  }

  /* Show the value currently being entered. */
  if (setting_mode) {
    printf("\n%s megadása: %s_\n",
           setting_mode == 'v' ? "Sebesség (1..5)" : "Banánlimit (25..99)",
           setting_text);

    puts("Enter = mentés | Backspace = törlés | Esc = mégse");
  }

  /* Keep warning messages visible after screen refreshes. */
  if (feedback[0]) {
    printf("\n%s\n", feedback);
  }

  fflush(stdout);
}

/* Compare two records so the best number of catches comes first. */
static int compare_scores(const void *left, const void *right) {
  const Score *a = left, *b = right;
  if (a->caught != b->caught)
    return (a->caught < b->caught) ? 1 : -1;
  return (a->fallen > b->fallen) - (a->fallen < b->fallen);
}

/* Read the text file and display a sorted leaderboard */
static void show_scores(void) {
  Score records[MAX_RECORDS];
  int count = 0;
  char line[256];
  FILE *file = fopen(score_file, "r");
  if (!file) {
    puts("Még nincs mentett eredmény.");
    return;
  }

  /* Reuse the tab-separated score format of the previous Linux client. */
  while (count < MAX_RECORDS && fgets(line, sizeof(line), file)) {
    Score *entry = &records[count];
    if (sscanf(line, "%19[^\t]\t%39[^\t]\t%d\t%d\t%d\t%d", entry->date,
               entry->name, &entry->caught, &entry->fallen, &entry->limit,
               &entry->speed) == 6 &&
        entry->fallen >= 0 && entry->fallen <= 99 && entry->caught >= 0 &&
        entry->caught <= entry->fallen && entry->speed >= 1 &&
        entry->speed <= 5 && entry->limit >= 25 && entry->limit <= 99)
      count++;
  }
  fclose(file);
  qsort(records, (size_t)count, sizeof(records[0]), compare_scores);
  puts("\n--- EREDMÉNYTÁBLA (elkapott banánok szerint) ---");
  puts(" #  Játékos                 Elkapott  Leesett  Seb. Limit");
  for (int i = 0; i < count && i < 10; ++i)
    printf("%2d  %-23.23s %8d %8d %5d %5d\n", i + 1, records[i].name,
           records[i].caught, records[i].fallen, records[i].speed,
           records[i].limit);
  if (count == 0)
    puts("Még nincs eredmény.");
  puts("\nA folytatáshoz nyomj meg egy gombot.");
  fflush(stdout);
}

/* Append one result using the same text format as the earlier program. */
static void save_score(void) {
  char date[20] = "ismeretlen";
  time_t now = time(NULL);
  struct tm *when = localtime(&now);
  if (when)
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M", when);

  FILE *file = fopen(score_file, "a");
  if (!file) {
    perror("Eredmény mentése");
    return;
  }
  if (fprintf(file, "%s\t%s\t%d\t%d\t%d\t%d\n", date, player, game.caught,
              game.fallen, game.limit, game.speed) < 0)
    perror("Eredmény írása");
  if (fclose(file) != 0)
    perror("Eredményfájl lezárása");
}

/* Parse an @STATE line; the firmware may echo keys before this marker. */
static void handle_line(const char *line) {
  const char *start = strstr(line, "@STATE ");
  if (!start)
    return;
  GameState next;
  char extra;
  if (sscanf(start, "@STATE %d %d %d %d %d %d %d %d %d %c", &next.basket,
             &next.banana, &next.phase, &next.fallen, &next.caught, &next.speed,
             &next.running, &next.finished, &next.limit, &extra) != 9)
    return;
  if (next.basket < 0 || next.basket >= TREES || next.banana < 0 ||
      next.banana >= TREES || next.phase < 0 || next.phase > 2 ||
      next.fallen < 0 || next.fallen > 99 || next.caught < 0 ||
      next.caught > next.fallen || next.speed < 1 || next.speed > 5 ||
      next.limit < 25 || next.limit > 99 ||
      (next.running != 0 && next.running != 1) ||
      (next.finished != 0 && next.finished != 1))
    return;

  /* A completed run is recorded once, not once per repeated status frame. */
  if (next.running && !game.running) {
    was_running = true;
    score_saved = false;
  }
  game = next;

  /* Clear the warning when the game stops or finishes. */
  if (!game.running) {
    setting_warning = false;
  }
  have_state = true;
  if (game.finished && was_running && !score_saved) {
    save_score();
    score_saved = true;
    was_running = false;
  }
  if (!game.running && !game.finished)
    was_running = false;
  draw_game();
}

/* UART data can arrive in fragments; collect characters until '\n'. */
static void receive_serial(void) {
  static char line[160];
  static size_t used = 0;
  char buffer[128];
  ssize_t size = read(serial_fd, buffer, sizeof(buffer));
  if (size <= 0) {
    if (size < 0 && errno == EINTR)
      return;
    puts("A soros kapcsolat megszakadt.");
    stopping = 1;
    return;
  }
  for (ssize_t i = 0; i < size; ++i) {
    char ch = buffer[i];
    if (ch == '\n') {
      line[used] = '\0';
      handle_line(line);
      used = 0;
    } else if (ch != '\r') {
      if (used + 1 < sizeof(line))
        line[used++] = ch;
      else
        used = 0; /* Ignore an excessively long corrupted line. */
    }
  }
}

/* Handle numeric input for speed and banana limit. */
static void handle_setting_key(char ch) {
  char command[16];

  if (ch == 27) {
    /* Escape cancels the setting. */
    setting_mode = '\0';
    snprintf(feedback, sizeof(feedback), "Beállítás megszakítva.");
  } else if (ch == 8 || ch == 127) {
    /* Backspace removes the last character. */
    if (setting_length > 0) {
      setting_text[--setting_length] = '\0';
    }
  } else if (ch == '\n' || ch == '\r') {
    /* Enter validates the value. */
    int value;
    char mode = setting_mode;
    setting_mode = '\0';

    if (!parse_number(setting_text, mode == 'v' ? 1 : 25, mode == 'v' ? 5 : 99,
                      &value)) {

      snprintf(feedback, sizeof(feedback),
               mode == 'v' ? "Hibás sebesség! Érvényes: 1..5."
                           : "Hibás banánlimit! Érvényes: 25..99.");
    } else {
      /* Send valid settings to the development board. */
      snprintf(command, sizeof(command), "%c%d\n", mode, value);

      if (send_command(command)) {
        feedback[0] = '\0';
      }
    }
  } else if (((ch >= '0' && ch <= '9') || (ch == '-' && setting_length == 0)) &&
             setting_length + 1 < sizeof(setting_text)) {

    /* Store digits without overflowing the buffer. */
    setting_text[setting_length++] = ch;
    setting_text[setting_length] = '\0';
  } else {
    snprintf(feedback, sizeof(feedback), "Csak egész számot adj meg!");
  }

  draw_game();
}

/* Process one key from the Linux terminal, including menu settings. */
static void handle_key(char ch) {
  char command[16];

  /* Handle player name editing. */
  if (editing_name) {
    if (ch == 27) {
      /* Escape cancels name editing. */
      editing_name = false;
    } else if (ch == '\r' || ch == '\n') {
      /* Enter saves the new name. */
      if (typed_length > 0) {
        strcpy(player, typed_name);
      }
      editing_name = false;
    } else if (ch == 8 || ch == 127) {
      /* Backspace removes the last character. */
      if (typed_length > 0) {
        typed_name[--typed_length] = '\0';
      }
    } else if ((unsigned char)ch >= 32 && ch != 127 &&
               typed_length + 1 < sizeof(typed_name)) {
      /* Append a character without overflowing the buffer. */
      typed_name[typed_length++] = ch;
      typed_name[typed_length] = '\0';
    }

    draw_game();
    return;
  }

  /* Close help when the user presses a key. */
  if (showing_help) {
    showing_help = false;
    draw_game();
    return;
  }

  /* Process numeric input after pressing v or m. */
  if (setting_mode) {
    handle_setting_key(ch);
    return;
  }

  /* Stop the running game before opening the help screen. */
  if (ch == 'h') {
    if (game.running && !send_command("x"))
      return;

    feedback[0] = '\0';
    showing_help = true;
    draw_game();
    return;
  }

  if (ch == 'q') {
    stopping = 1;
    return;
  }
  if (ch == 't') {
    show_scores();
    return;
  }
  /* Display the current state without requesting it again over UART. */
  if (ch == '?') {
    if (have_state) {
      draw_game();
    } else {
      puts("Még nem érkezett állapot a fejlesztőkártyától.");
    }
    return;
  }

  /* Send game control commands over UART. */
  if (ch == 's' || ch == 'b' || ch == 'j' || ch == 'x') {

    command[0] = ch;
    command[1] = '\0';

    feedback[0] = '\0';
    send_command(command);
    return;
  }

  /* Ignore keys that are not configuration commands. */
  if (ch != 'v' && ch != 'm' && ch != '+' && ch != '-') {
    return;
  }

  /* Wait until the initial state arrives from the board. */
  if (!have_state) {
    snprintf(feedback, sizeof(feedback),
             "Várakozás a fejlesztőkártya állapotára.");
    draw_game();
    return;
  }

  /* Do not change settings during gameplay. */
  if (game.running) {
    snprintf(feedback, sizeof(feedback),
             "A beállítások csak játék előtt módosíthatók!");
    draw_game();
    return;
  }

  /* Start numeric input for speed or banana limit. */
  if (ch == 'v' || ch == 'm') {
    setting_mode = ch;
    setting_length = 0;
    setting_text[0] = '\0';
    feedback[0] = '\0';

    draw_game();
    return;
  }

  /* Increase or decrease the speed by one level. */
  int next_speed = game.speed + (ch == '+' ? 1 : -1);

  if (next_speed < 1 || next_speed > 5) {
    snprintf(feedback, sizeof(feedback),
             "A sebesség csak 1 és 5 között lehet!");
  } else {
    char command[16];

    snprintf(command, sizeof(command), "v%d\n", next_speed);

    if (send_command(command)) {
      snprintf(feedback, sizeof(feedback), "Sebességmódosítás elküldve: %d/5",
               next_speed);
    }
  }

  draw_game();
}

int main(int argc, char *argv[]) {
  /* Display usage when no command-line arguments are provided. */
  if (argc == 1) {
    help(argv[0]);
    return 0;
  }

  /* The -d/-v/-m/-n/-f/-h switches between the functions*/
  int option;
  while ((option = getopt(argc, argv, "hsv:m:n:t:")) != -1) {
    switch (option) {
    case 'h':
      help(argv[0]);
      return 0;
    case 's':
      auto_start = true;
      break;
    case 'v':
      speed_given = true;
      if (!parse_number(optarg, 1, 5, &start_speed))
        return fprintf(stderr, "Sebesség: 1..5\n"), 1;
      break;
    case 'm':
      limit_given = true;
      if (!parse_number(optarg, 25, 99, &start_limit))
        return fprintf(stderr, "Banánlimit: 25..99\n"), 1;
      break;
    case 'n':
      // Allow spaces, but reject tabs and line breaks.
      if (optarg[0] == '\0' || strlen(optarg) >= sizeof(player) ||
          strpbrk(optarg, "\t\r\n"))
        return fprintf(stderr, "Érvénytelen játékosnév (max. 39 bájt).\n"), 1;

      strcpy(player, optarg);
      break;

    case 't':
      if (strlen(optarg) >= sizeof(score_file))
        return fprintf(stderr, "Túl hosszú fájlnév.\n"), 1;
      strcpy(score_file, optarg);
      break;
    default:
      help(argv[0]);
      return 1;
    }
  }
  if (optind != argc) {
    help(argv[0]);
    return 1;
  }

  serial_fd = open_serial(device);
  if (serial_fd < 0) {
    perror(device);
    return 1;
  }

  /* Noncanonical mode delivers b/j keys immediately without Enter. */
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old_terminal) == 0) {
    struct termios raw = old_terminal;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
      terminal_changed = true;
  }
  signal(SIGINT, stop_handler);
  signal(SIGTERM, stop_handler);
  /* Reset the board before automatic start. */
  if (auto_start) {
    puts("Kapcsolódva. Játék indítása.");
    send_command("x");
  } else {
    puts("Kapcsolódva. Az indításhoz nyomd meg az s gombot.");
  }

  /* Apply the selected speed. */
  if (!stopping && speed_given) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "v%d\n", start_speed);
    send_command(cmd);
  }

  /* Apply the selected banana limit. */
  if (!stopping && limit_given) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "m%d\n", start_limit);
    send_command(cmd);
  }

  /* Request the current game state. */
  if (!stopping)
    send_command("?");

  /* Start the game after applying all command-line settings. */
  if (!stopping && auto_start) {
    send_command("s");
  }

  /* Start the game automatically if -s was specified. */
  if (!stopping && auto_start)
    send_command("s");

  struct pollfd inputs[2] = {{.fd = STDIN_FILENO, .events = POLLIN},
                             {.fd = serial_fd, .events = POLLIN}};
  bool show_game_after_table = false;
  while (!stopping) {
    int ready = poll(inputs, 2, 500);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      perror("poll");
      break;
    }
    if (inputs[1].revents & (POLLERR | POLLHUP | POLLNVAL))
      break;
    if (inputs[1].revents & POLLIN)
      receive_serial();
    if (inputs[0].revents & POLLIN) {
      char keys[32];
      ssize_t count = read(STDIN_FILENO, keys, sizeof(keys));
      if (count <= 0)
        inputs[0].fd = -1; /* No terminal input available. */
      else
        for (ssize_t i = 0; i < count && !stopping; ++i) {
          /* Allow Enter while editing the player name. */
          /* Enter is needed for name and numeric setting input. */
          if (!editing_name && !setting_mode && !showing_help &&
              (keys[i] == '\n' || keys[i] == '\r')) {
            continue;
          }

          /* Check whether the score table is being opened. */
          bool table_requested =
              !editing_name && !setting_mode && !showing_help && keys[i] == 't';

          handle_key(keys[i]);

          if (table_requested) {
            show_game_after_table = true;
          } else if (show_game_after_table && !showing_help) {
            draw_game();
            show_game_after_table = false;
          }
        }
    }
    if (inputs[0].revents & (POLLERR | POLLHUP | POLLNVAL))
      inputs[0].fd = -1;
  }

  /* 'x' resets the board when the Linux program exits. */
  if (serial_fd >= 0) {
    (void)write(serial_fd, "x", 1);
    (void)tcdrain(serial_fd);
    close(serial_fd);
  }
  if (terminal_changed)
    tcsetattr(STDIN_FILENO, TCSANOW, &old_terminal);
  puts("\nA Linux-program kilépett, a játék leállítva.");
  return 0;
}