#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "types.h"
#include "file.h"

#define SEGMENT (1<<0)
#define OPCODE  (1<<1)
#define STRING  (1<<2)
#define RVALUE  (1<<3)

#define APP_VERSION "0.0.1c"

typedef struct {
  char *sample;
  char *pitch_keycenter, *lokey, *hikey;
  int lovel, hivel;
} note_t;

int DRY_RUN = 0;
char *MIDI[128];
note_t global;
char destdir[513];

/* convert string to uppercase */
char *strtoupper (char *str) {
  char *ptr;
    for (ptr = str; *ptr; ptr++) { *ptr = toupper(*ptr); }
  return (str);
}

/* convert string to lowercase */
char *strtolower (char *str) {
  char *ptr;
    for (ptr = str; *ptr; ptr++) { *ptr = tolower(*ptr); }
  return (str);
}

int create_midi_table (char **MIDI) {
  int i, j;
  char *NOTES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  char note[5];

/* generate MIDI notes table */
  for (j = 0; j < 11; j++) {
    for (i = 0; i < 12; i++) {
      snprintf(note, 5, "%s%d", NOTES[i], j - 2);
      MIDI[j * 12 + i] = strdup(note);
      if (j * 12 + i == 127)
        break;
    }
  }

  return 0;
}

int process_sfz (char *sfz, char *destdir) {
  note_t *SAMPLES;
  file_t *F;
  unsigned int FLAG = 0;
  long samples = -1, regions, i, j;
  char *ptr, dirname[513], ddirname[513], fname[256], *segment, *VAR = NULL, *VAL = NULL, src[256], dst[256], subdir[256], path[256], *fptr;
  FILE *out; /* FIXME: one libefile has write function, use that one */
  struct stat sb;

  if (!file_open(&F, F_READ, sfz)) {

/* count regions */
    for (ptr=(char *)F->buffer, regions=0; ptr-(char *)F->buffer<F->size; ptr++) {
      if (!strncasecmp (ptr, "<region>", 8)) regions++;
    }

    SAMPLES = (note_t *)malloc (sizeof(note_t)*regions);

#ifdef DEBUG
    fprintf (stderr, "%d regions found.\n", regions);
#endif

/* create subdir and write output to file */
    if (!DRY_RUN) {
      snprintf (dirname, 512, "%s/%s", destdir, sfz); *(strrchr (dirname, '.')) = 0;
      for (ptr=dirname, i=0; ; i++) {
        if ((dirname[i] == '/' && i)||!dirname[i]) {
          strcpy (ddirname, dirname);
          ddirname[i] = 0;
          if (stat (ddirname, &sb)) {
            mkdir (ddirname, (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
          }
        }
        if (!dirname[i]) break;
      }

      snprintf (fname, 255, "%s/%s", dirname, sfz);
      out = fopen (fname, "wb");
      if (!out) {
        fprintf (stderr, "Cannot create file '%s'\n", fname);
        goto cancel;
      }
    } else out = stderr;

/* parse the buffer */
    for (i = 0; i < F->size; i++) {
      if (!(FLAG & STRING)) {

/* skip comment lines */
        if (F->buffer[i] == '/' && F->buffer[i + 1] == '/') {
          while (F->buffer[i] && F->buffer[i] != '\r' && F->buffer[i] != '\n') { i++; }
          ptr = (char *)F->buffer + i;
          continue;
        }

/* new segment */
        if (F->buffer[i] == '<') {
          FLAG |= SEGMENT;
          F->buffer[i] = 0;
          segment = (char *)F->buffer + i + 1;
        }

        if (F->buffer[i] == '>') {
          if (FLAG & SEGMENT) { FLAG -= SEGMENT; FLAG |= OPCODE; }
          F->buffer[i] = 0;
        }

/* set variable name and type */
        if (F->buffer[i] == '=') {
          F->buffer[i] = 0;
          for (j = i - 1; isspace(F->buffer[j]); j--) { F->buffer[j] = 0; }
          VAR = ptr;
          FLAG |= RVALUE;
          if (!strcasecmp(VAR, "sample")) { FLAG |= STRING; }
          ptr = (char *)F->buffer + i + 1;
          continue;
        }

        if (isspace(F->buffer[i])) { F->buffer[i] = 0; }
      }

/* newline */
      if (F->buffer[i] == '\r' || F->buffer[i] == '\n') {
        F->buffer[i] = 0;
        if (FLAG & STRING) { FLAG -= STRING; }
      }

      if (!F->buffer[i]) {
/* remove space characters */
        while (isspace(*ptr)) { ptr++; }
        for (j=i-1; isspace(F->buffer[j]); j--) { F->buffer[j]=0; }

/* set value of variable */
        if (FLAG & RVALUE) {
          FLAG -= RVALUE;
          VAL = ptr;
          while (isspace(*VAL)) { VAL++; }
        }

        if (ptr != (char *)&F->buffer[i]) {
          if (FLAG & OPCODE) {
            if (!strcasecmp(ptr, "region")) {
              samples++;

/* clear variables */
              SAMPLES[samples].lokey = 0;
              SAMPLES[samples].hikey = 0;
              SAMPLES[samples].lovel = 0;
              SAMPLES[samples].hivel = 0;
              SAMPLES[samples].pitch_keycenter = 0;

#ifdef DEBUG
              fprintf(stderr, "\n");
#endif
            }
            if (FLAG & OPCODE) { FLAG -= OPCODE; }
          } else {
            if (samples >= 0 || !strcasecmp(segment, "group")) {
              if (!strcasecmp(VAR, "sample")) { SAMPLES[samples].sample = strdup(VAL); }
              if (!strcasecmp(VAR, "pitch_keycenter") || !strcasecmp(VAR, "key")) {
                if (isdigit(*VAL)) { SAMPLES[samples].pitch_keycenter = strdup(MIDI[atoi(VAL)]); } else { SAMPLES[samples].pitch_keycenter = VAL; }
              }
              if (!strcasecmp(VAR, "lokey")) { if (isdigit(*VAL)) { SAMPLES[samples].lokey = strdup(MIDI[atoi(VAL)]); } else { SAMPLES[samples].lokey = VAL; } }
              if (!strcasecmp(VAR, "hikey")) { if (isdigit(*VAL)) { SAMPLES[samples].hikey = strdup(MIDI[atoi(VAL)]); } else { SAMPLES[samples].hikey = VAL; } }
              if (!strcasecmp(VAR, "lovel")) { if (!strcasecmp(segment, "group")) { global.lovel = atoi(VAL); } else { SAMPLES[samples].lovel = atoi(VAL); } }
              if (!strcasecmp(VAR, "hivel")) { if (!strcasecmp(segment, "group")) { global.hivel = atoi(VAL); } else { SAMPLES[samples].hivel = atoi(VAL); } }
            }
          }
#ifdef DEBUG
          if (VAR && VAL) { if (!(FLAG & OPCODE)) { fprintf(stderr, "[%s] %s = %s\n", segment, VAR, VAL); } }
#endif
        }
        ptr = (char *)F->buffer + i + 1;
      }
    }

    if (samples >= 0) {
#ifdef DEBUG
      fprintf(stderr, "\n\nsamples:    %ld\n", samples + 1);
#endif
      for (i = 0; i < samples + 1; i++) {

        snprintf (src, 255, "[ %ld ] %s \r", samples-i, sfz);
        write (1, src, strlen(src)+1);

/* use global values of available */
        if (global.lovel != -1) { SAMPLES[i].lovel = global.lovel; }
        if (global.hivel != -1) { SAMPLES[i].hivel = global.hivel; }

/* use default values if not set */
        if (!SAMPLES[i].lovel) { SAMPLES[i].lovel = 0; }
        if (!SAMPLES[i].hivel) { SAMPLES[i].hivel = 127; }

/* fix broken SFZs */
        if (SAMPLES[i].lovel == 1) { SAMPLES[i].lovel = 0; }
        if (SAMPLES[i].hivel > 126) { SAMPLES[i].hivel = 127; }

        ptr = strrchr(SAMPLES[i].sample, '.');
        *ptr = 0;
        ptr++;
        snprintf(subdir, 255, "%d-%d", SAMPLES[i].lovel, SAMPLES[i].hivel);

        if (SAMPLES[i].pitch_keycenter) {
          snprintf(fname, 255, "%s %d-%d %s.%s", SAMPLES[i].sample, SAMPLES[i].lovel, SAMPLES[i].hivel, SAMPLES[i].pitch_keycenter, ptr);
        } else {
          snprintf(fname, 255, "%s %d-%d.%s", SAMPLES[i].sample, SAMPLES[i].lovel, SAMPLES[i].hivel, ptr);
        }

        for (fptr = fname; *fptr; fptr++) {
          if (*fptr == ' ') { *fptr = '_'; }
        }

#ifdef DEBUG
        fprintf(stderr, "\nsample:    '%s'\n", SAMPLES[i].sample);
        fprintf(stderr, "keycenter: '%s'\n", SAMPLES[i].pitch_keycenter);
        fprintf(stderr, "lokey:     '%s'\n", SAMPLES[i].lokey);
        fprintf(stderr, "hikey:     '%s'\n", SAMPLES[i].hikey);
        fprintf(stderr, "lovel:      %d\n", SAMPLES[i].lovel);
        fprintf(stderr, "hivel:      %d\n", SAMPLES[i].hivel);
#else
        fprintf(out, "\n<region>\n");
/*
* '/' helyett '\' az NI kontakt miatt, kulonben nem tud mit kezdeni az
* SFZ-vel; megnyitja ugyan, de nem tudja menteni/konvertalni
*/
        fprintf(out, "sample=%s\\%s\n", subdir, strtolower(fname));
        if (SAMPLES[i].lokey) { fprintf(out, "lokey=%s\n", SAMPLES[i].lokey); }
        if (SAMPLES[i].hikey) { fprintf(out, "hikey=%s\n", SAMPLES[i].hikey); }
        fprintf(out, "lovel=%d\n", SAMPLES[i].lovel);
        fprintf(out, "hivel=%d\n", SAMPLES[i].hivel);
        if (SAMPLES[i].pitch_keycenter) { fprintf(out, "pitch_keycenter=%s\n", SAMPLES[i].pitch_keycenter); }
#endif
        if (!DRY_RUN) {
          snprintf (path, 255, "%s/%s", dirname, subdir);
          mkdir (path, (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
          snprintf (src, 255, "%s.%s", SAMPLES[i].sample, ptr);
          snprintf (dst, 255, "%s/%s", path, strtolower(fname));
          file_copy (src, dst);
        } else {
          printf("mkdir %s 2>/dev/null ; mv -v \"%s.%s\" \"%s/%s\"\n", subdir, SAMPLES[i].sample, ptr, subdir, strtolower(fname));
        }
      }
    }

cancel:
    free (SAMPLES);
    file_close(&F);
    if (!DRY_RUN) { fclose (out); }

    printf ("\r%s          \r\n", sfz);
  }

  return 0;
}

int listdir (char *dir) {
  DIR *D;
  struct dirent *entry;
  char *ptr, cwd[513], target[513];
  struct stat sb;
  int r = 0;

  stat (dir, &sb);
  if (S_ISDIR(sb.st_mode)) {
    if ((D = opendir (dir))) {
      while ((entry = readdir (D))) {
        if (*entry->d_name == '.') continue;
        if (entry->d_type & DT_DIR) { 
          snprintf (target, 512, "%s/%s", dir, entry->d_name);
          printf ("\r          \r\033[01;37m%s/%s\033[22;37m\n", dir, entry->d_name);
          listdir (target); 
        }
        ptr = strrchr (entry->d_name, '.');
        if (!ptr) continue;
        if (!strcasecmp (ptr, ".sfz")) {
          getcwd (cwd, 512);
          chdir (dir);
          if (!strcmp(dir, ".")) { *dir = 0; }
          snprintf (target, 512, "%s%s", destdir, dir);
          process_sfz (entry->d_name, target);
          chdir (cwd);
        }
      }
      closedir (D);
    } else r = -1;
  } else {
    process_sfz (dir, destdir);
  }

  return (r);
}

int main(int argc, char **argv) {
  int o = 1;

/* usage information */
  if (argc == 1) {
    fprintf (stderr, "\033[01;37mSFZ %s\n", APP_VERSION);
    fprintf (stderr, "Copyright (c) 2012 by DiCE/PsychoMix\n");
    fprintf (stderr, "http://www.psychomix.org\033[22;37m\n");

    fprintf(stderr, "\nusage: %s [--dry-run, --destdir '/path/to/destination'] filename\n", argv[0]);
    exit(-1);
  }

  *destdir = 0;

  create_midi_table (MIDI);

/* global defaults */
  global.lovel = -1;
  global.hivel = -1;

  for (o=1; o<argc; o++) {
/* parse cmdline options */
    if (*argv[o] == '-') {
      if (!strcmp (argv[o], "--dry-run")) { DRY_RUN = 1; }
      if (!strcmp (argv[o], "--destdir")) { snprintf (destdir, 512, "%s/", argv[++o]); }
      continue;
    }
  }

  for (o=1; o<argc; o++) {
    if (!strcmp (argv[o], "--destdir")) { o++; continue; }
    if (*argv[o] != '-') { listdir (argv[o]); }
  }

  return 0;
}
