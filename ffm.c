#include <curses.h>
#include <dirent.h>
#include <eti.h>
#include <langinfo.h>
#include <linux/limits.h>
#include <locale.h>
#include <menu.h>
#include <ncursesw/ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILELIST_LEN 128

#define ENTER 10

enum SORT_TYPE { ALPHABETICAL, FILESIZE };

struct FILE_ENTRY {
                char name[FILENAME_MAX];
                char date[256];
                char path[PATH_MAX];
                char display_info[812];
                unsigned char type;
                __off_t size;
};

struct FILE_LIST {
                int filecount;
                struct FILE_ENTRY *files;
};

char *readable_fs(double size, unsigned char type, char *buf) {
        int i = 0;
        const char *units[] = {"B",  "kB", "MB", "GB", "TB",
                               "PB", "EB", "ZB", "YB"};
        while (size >= 1024 && i <= 8) {
                size /= 1024;
                i++;
        }
        if (type != DT_DIR)
                snprintf(buf, 32, "%.*f %s", i, size, units[i]);
        else
                sprintf(buf, "<DIR>");
        return buf;
}

static int cmp_entry_alphabetical(const void *p1, const void *p2) {
        const struct FILE_ENTRY *f1 = (struct FILE_ENTRY *)p1;
        const struct FILE_ENTRY *f2 = (struct FILE_ENTRY *)p2;

        return strcmp(f1->name, f2->name);
}

static int cmp_entry_filesize(const void *p1, const void *p2) {
        struct FILE_ENTRY *f1 = (struct FILE_ENTRY *)p1;
        struct FILE_ENTRY *f2 = (struct FILE_ENTRY *)p2;

        if (f1->size == f2->size)
                return 0;
        else if (f1->size < f2->size)
                return 1;
        else
                return -1;
}

static void format_time_string(time_t mtime, char *buffer, size_t size) {
        struct tm *tm = localtime(&mtime);
        strftime(buffer, size, nl_langinfo(D_T_FMT), tm);
}

static int resolve_absolute_path(const char *filename, char *path_buffer,
                                 mode_t mode) {
        if (realpath(filename, path_buffer)) {
                return 1;
        }

        if (S_ISLNK(mode)) {
                path_buffer[0] = '\0';
                return 0;
        }

        perror("realpath");
        exit(EXIT_FAILURE);
}

static int ensure_list_capacity(struct FILE_LIST *fl, int *capacity) {
        if (fl->filecount < *capacity)
                return 0;
        *capacity *= 2;

        struct FILE_ENTRY *tmp =
            realloc(fl->files, *capacity * sizeof(struct FILE_ENTRY));
        if (!tmp) {
                perror("realloc");
                return -1;
        }

        fl->files = tmp;
        return 0;
}

struct FILE_ENTRY get_file_entry(struct dirent *dp) {
        struct FILE_ENTRY file;
        struct stat st;
        char size_str[32];

        if (lstat(dp->d_name, &st) == -1) {
                perror("lstat");
                exit(EXIT_FAILURE);
        }

        snprintf(file.name, sizeof(file.name), "%s", dp->d_name);
        file.size = st.st_size;
        file.type = dp->d_type;

        format_time_string(st.st_mtime, file.date, sizeof(file.date));
        readable_fs(st.st_size, file.type, size_str);

        snprintf(file.display_info, sizeof(file.display_info), "%8s  |  %s",
                 size_str, file.date);

        if (!resolve_absolute_path(dp->d_name, file.path, st.st_mode)) {
                snprintf(file.display_info, 15, "broken symlink");
        }

        return file;
}

struct FILE_LIST get_files_in_directory(const char *dirpath) {
        struct FILE_LIST fl = {.files = NULL, .filecount = 0};
        struct dirent *dp;
        DIR *dirp;
        int capacity = MAX_FILELIST_LEN;

        if ((dirp = opendir(dirpath)) == NULL) {
                perror("opendir");
                exit(EXIT_FAILURE);
        }

        fl.files = malloc(capacity * sizeof(struct FILE_ENTRY));
        if (!fl.files) {
                perror("malloc");
                closedir(dirp);
                exit(EXIT_FAILURE);
        }

        while ((dp = readdir(dirp)) != NULL) {
                if (strcmp(dp->d_name, ".") == 0)
                        continue;

                if (strcmp(dp->d_name, "..") == 0)
                        continue;

                if (ensure_list_capacity(&fl, &capacity) == -1) {
                        free(fl.files);
                        closedir(dirp);
                        exit(EXIT_FAILURE);
                }

                fl.files[fl.filecount++] = get_file_entry(dp);
        }

        closedir(dirp);
        return fl;
}

void free_filelist(struct FILE_LIST *fl) {
        if (!fl->files)
                return;
        free(fl->files);
}

void free_curses_items(ITEM **items, int itemcount) {
        if (!items)
                return;

        for (int i = 0; i < itemcount; i++) {
                free_item(items[i]);
        }
        free(items);
}

void enable_visual_mode() {
        setlocale(LC_ALL, "");
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
}

void disable_visual_mode() {
        if (!isendwin())
                endwin();
}

ITEM **create_curses_file_items(struct FILE_LIST *fl) {
        ITEM **items;
        int i;

        items = calloc(fl->filecount + 1, sizeof(ITEM *));
        if (!items) {
                perror("calloc");
                exit(EXIT_FAILURE);
        }

        for (i = 0; i < fl->filecount; i++) {
                if ((items[i] = new_item(fl->files[i].name,
                                         fl->files[i].display_info)) == NULL) {
                        perror(fl->files[i].display_info);
                        exit(EXIT_FAILURE);
                }
                set_item_userptr(items[i], &fl->files[i]);
        }
        items[fl->filecount] = NULL;

        return items;
}

void load_directory_menu(const char *target_path, MENU **menu, ITEM ***items,
                         struct FILE_LIST *fl, int sort) {
        char buf[4096];
        char cwd[PATH_MAX];

        strcpy(buf, target_path);
        if (strcmp(target_path, "")) {
                chdir(buf);
        }

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
                perror("cwd");
                exit(EXIT_FAILURE);
        }

        mvprintw(LINES - 3, 0, "Current dir: %s", cwd);
        mvprintw(LINES - 2, 0, "Press q to quit");

        *fl = get_files_in_directory(".");

        switch (sort) {
        case ALPHABETICAL:
                qsort(fl->files, fl->filecount, sizeof(struct FILE_ENTRY),
                      cmp_entry_alphabetical);
                break;
        case FILESIZE:
                qsort(fl->files, fl->filecount, sizeof(struct FILE_ENTRY),
                      cmp_entry_filesize);
                break;
        }

        *items = create_curses_file_items(fl);
        *menu = new_menu(*items);
        post_menu(*menu);
        refresh();
}

void unload_directory_menu(MENU *menu, ITEM **items, struct FILE_LIST *fl) {
        unpost_menu(menu);
        free_menu(menu);
        free_curses_items(items, fl->filecount);
        free_filelist(fl);
}

struct FILE_ENTRY get_item_file(ITEM *item) {
        struct FILE_ENTRY file;
        file = *(struct FILE_ENTRY *)item_userptr(item);
        return file;
}

int main(int argc, char *argv[]) {
        int c;
        struct FILE_LIST fl;
        ITEM **items;
        MENU *menu;
        char current_dir[PATH_MAX];
        int current_sort;
        bool should_reload;
        struct FILE_ENTRY curr_file;

        current_sort = ALPHABETICAL;
        should_reload = 1;

        if (atexit(disable_visual_mode) != 0) {
                perror("atexit");
                exit(EXIT_FAILURE);
        }

        const char *start_path = (argc == 2) ? argv[1] : ".";

        if (realpath(start_path, current_dir) == NULL) {
                perror("realpath");
                exit(EXIT_FAILURE);
        }

        enable_visual_mode();

        while (1) {
                if (should_reload) {
                        if (fl.files != NULL) {
                                unload_directory_menu(menu, items, &fl);
                        }
                        load_directory_menu(current_dir, &menu, &items, &fl,
                                            current_sort);
                        should_reload = false;
                }

                c = getch();
                if (c == 'q')
                        break;

                switch (c) {
                case KEY_DOWN:
                        menu_driver(menu, REQ_DOWN_ITEM);
                        break;

                case KEY_UP:
                        menu_driver(menu, REQ_UP_ITEM);
                        break;
                case KEY_LEFT:
                        strncpy(current_dir, "..", 3);
                        should_reload = true;
                        break;

                case KEY_RIGHT:
                case ENTER:
                        curr_file = get_item_file(current_item(menu));
                        if (curr_file.type == DT_DIR ||
                            curr_file.type == DT_LNK) {
                                strncpy(current_dir, curr_file.path, PATH_MAX);
                                should_reload = true;
                        }
                        break;

                case 's':
                        if (current_sort != FILESIZE) {
                                current_sort = FILESIZE;
                                should_reload = true;
                        }
                        break;

                case 'a':
                        if (current_sort != ALPHABETICAL) {
                                current_sort = ALPHABETICAL;
                                should_reload = true;
                        }
                        break;

                case KEY_RESIZE:
                        should_reload = true;
                        break;
                }
        }

        unload_directory_menu(menu, items, &fl);
        return 0;
}