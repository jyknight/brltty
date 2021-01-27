/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2021 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include "prologue.h"

#include <string.h>
#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "log.h"
#include "messages.h"
#include "file.h"

// MinGW doesn't define LC_MESSAGES
#ifndef LC_MESSAGES
#define LC_MESSAGES LC_ALL
#endif /* LC_MESSAGES */

// Windows needs O_BINARY
#ifndef O_BINARY
#define O_BINARY 0
#endif /* O_BINARY */

static char *localeDirectory = NULL;
static char *localeSpecifier = NULL;
static char *domainName = NULL;

const char *
getMessagesDirectory (void) {
  return localeDirectory;
}

const char *
getMessagesLocale (void) {
  return localeSpecifier;
}

const char *
getMessagesDomain (void) {
  return domainName;
}

static const uint32_t magicNumber = UINT32_C(0X950412DE);
typedef uint32_t GetIntegerFunction (uint32_t value);

typedef struct {
  uint32_t magicNumber;
  uint32_t versionNumber;
  uint32_t messageCount;
  uint32_t originalMessages;
  uint32_t translatedMessages;
  uint32_t hashSize;
  uint32_t hashOffset;
} MessagesHeader;

typedef struct {
  union {
    void *area;
    const unsigned char *bytes;
    const MessagesHeader *header;
  } view;

  size_t areaSize;
  GetIntegerFunction *getInteger;
} MessagesData;

static MessagesData messagesData = {
  .view.area = NULL,
  .areaSize = 0,
  .getInteger = NULL,
};

static uint32_t
getNativeInteger (uint32_t value) {
  return value;
}

static uint32_t
getFlippedInteger (uint32_t value) {
  uint32_t result = 0;

  while (value) {
    result <<= 8;
    result |= value & UINT8_MAX;
    value >>= 8;
  }

  return result;
}

static int
checkMagicNumber (MessagesData *data) {
  const MessagesHeader *header = data->view.header;

  {
    static GetIntegerFunction *const functions[] = {
      getNativeInteger,
      getFlippedInteger,
      NULL
    };

    GetIntegerFunction *const *function = functions;

    while (*function) {
      if ((*function)(header->magicNumber) == magicNumber) {
        data->getInteger = *function;
        return 1;
      }

      function += 1;
    }
  }

  return 0;
}

static char *
makeLocaleDirectoryPath (void) {
  size_t length = strlen(localeSpecifier);

  char dialect[length + 1];
  strcpy(dialect, localeSpecifier);
  length = strcspn(dialect, ".@");
  dialect[length] = 0;

  char language[length + 1];
  strcpy(language, dialect);
  length = strcspn(language, "_");
  language[length] = 0;

  char *codes[] = {dialect, language, NULL};
  char **code = codes;

  while (*code && **code) {
    char *path = makePath(localeDirectory, *code);

    if (path) {
      if (testDirectoryPath(path)) return path;
      free(path);
    }

    code += 1;
  }

  logMessage(LOG_WARNING, "messages locale not found: %s", localeSpecifier);
  return NULL;
}

static char *
makeMessagesFilePath (void) {
  char *locale = makeLocaleDirectoryPath();

  if (locale) {
    char *category = makePath(locale, "LC_MESSAGES");

    free(locale);
    locale = NULL;

    if (category) {
      char *file = makeFilePath(category, domainName, ".mo");

      free(category);
      category = NULL;

      if (file) return file;
    }
  }

  return NULL;
}

int
loadMessagesData (void) {
  if (messagesData.view.area) return 1;
  ensureAllMessagesProperties();

  int loaded = 0;
  char *path = makeMessagesFilePath();

  if (path) {
    int fd = open(path, (O_RDONLY | O_BINARY));

    if (fd != -1) {
      struct stat info;

      if (fstat(fd, &info) != -1) {
        size_t size = info.st_size;
        void *area = NULL;

        if (size) {
          if ((area = malloc(size))) {
            ssize_t count = read(fd, area, size);

            if (count == -1) {
              logMessage(LOG_WARNING,
                "messages data read error: %s: %s",
                path, strerror(errno)
              );
            } else if (count < size) {
              logMessage(LOG_WARNING,
                "truncated messages data: %"PRIssize" < %"PRIsize": %s",
                count, size, path
              );
            } else {
              MessagesData data = {
                .view.area = area,
                .areaSize = size
              };

              if (checkMagicNumber(&data)) {
                messagesData = data;
                area = NULL;
                loaded = 1;
              }
            }

            if (!loaded) free(area);
          } else {
            logMallocError();
          }
        } else {
          logMessage(LOG_WARNING, "no messages data");
        }
      } else {
        logMessage(LOG_WARNING,
          "messages file stat error: %s: %s",
          path, strerror(errno)
        );
      }

      close(fd);
    } else {
      logMessage(LOG_WARNING,
        "messages file open error: %s: %s",
        path, strerror(errno)
      );
    }

    free(path);
  }

  return loaded;
}

void
releaseMessagesData (void) {
  if (messagesData.view.area) free(messagesData.view.area);
  memset(&messagesData, 0, sizeof(messagesData));
}

static inline const MessagesHeader *
getHeader (void) {
  return messagesData.view.header;
}

static inline const void *
getItem (uint32_t offset) {
  return &messagesData.view.bytes[messagesData.getInteger(offset)];
}

uint32_t
getMessageCount (void) {
  return messagesData.getInteger(getHeader()->messageCount);
}

struct MessageStruct {
  uint32_t length;
  uint32_t offset;
};

uint32_t
getMessageLength (const Message *message) {
  return messagesData.getInteger(message->length);
}

const char *
getMessageText (const Message *message) {
  return getItem(message->offset);
}

static inline const Message *
getOriginalMessages (void) {
  return getItem(getHeader()->originalMessages);
}

static inline const Message *
getTranslatedMessages (void) {
  return getItem(getHeader()->translatedMessages);
}

const Message *
getOriginalMessage (unsigned int index) {
  return &getOriginalMessages()[index];
}

const Message *
getTranslatedMessage (unsigned int index) {
  return &getTranslatedMessages()[index];
}

const char *
getMessagesMetadata (void) {
  if (getMessageCount() == 0) return "";

  const Message *original = getOriginalMessage(0);
  if (getMessageLength(original) != 0) return "";

  return getMessageText(getTranslatedMessage(0));
}

int
findOriginalMessage (const char *text, size_t textLength, unsigned int *index) {
  const Message *messages = getOriginalMessages();
  int from = 0;
  int to = getMessageCount();

  while (from < to) {
    int current = (from + to) / 2;
    const Message *message = &messages[current];

    uint32_t messageLength = getMessageLength(message);
    int relation = memcmp(text, getMessageText(message), MIN(textLength, messageLength));

    if (relation == 0) {
      if (textLength == messageLength) {
        *index = current;
        return 1;
      }

      relation = (textLength < messageLength)? -1: 1;
    }

    if (relation < 0) {
      to = current;
    } else {
      from = current + 1;
    }
  }

  return 0;
}

const Message *
findSimpleTranslation (const char *text, size_t length) {
  if (!text) return NULL;
  if (!length) return NULL;

  if (loadMessagesData()) {
    unsigned int index;

    if (findOriginalMessage(text, length, &index)) {
      return getTranslatedMessage(index);
    }
  }

  return NULL;
}

const char *
getSimpleTranslation (const char *text) {
  const Message *translation = findSimpleTranslation(text, strlen(text));
  if (translation) return getMessageText(translation);
  return text;
}

const Message *
findPluralTranslation (const char *const *strings) {
  unsigned int count = 0;
  while (strings[count]) count += 1;
  if (!count) return NULL;

  size_t size = 0;
  size_t lengths[count];

  for (unsigned int index=0; index<count; index+=1) {
    size_t length = strlen(strings[index]);
    lengths[index] = length;
    size += length + 1;
  }

  char text[size];
  char *byte = text;

  for (unsigned int index=0; index<count; index+=1) {
    byte = mempcpy(byte, strings[index], (lengths[index] + 1));
  }

  byte -= 1; // the length mustn't include the final NUL
  return findSimpleTranslation(text, (byte - text));
}

const char *
getPluralTranslation (const char *singular, const char *plural, unsigned long int count) {
  int useSingular = count == 1;

  const char *const strings[] = {singular, plural, NULL};
  const Message *message = findPluralTranslation(strings);
  if (!message) return useSingular? singular: plural;

  const char *translation = getMessageText(message);
  if (!useSingular) translation += strlen(translation) + 1;
  return translation;
}

#ifdef ENABLE_I18N_SUPPORT
static int
setDirectory (const char *directory) {
  if (bindtextdomain(domainName, directory)) return 1;
  logSystemError("bindtextdomain");
  return 0;
}

static int
setDomain (const char *domain) {
  if (!textdomain(domain)) {
    logSystemError("textdomain");
    return 0;
  }

  if (!bind_textdomain_codeset(domain, "UTF-8")) {
    logSystemError("bind_textdomain_codeset");
  }

  return 1;
}
#else /* ENABLE_I18N_SUPPORT */
static int
setDirectory (const char *directory) {
  return 1;
}

static int
setDomain (const char *domain) {
  return 1;
}

char *
gettext (const char *text) {
  return (char *)getSimpleTranslation(text);
}

char *
ngettext (const char *singular, const char *plural, unsigned long int count) {
  return (char *)getPluralTranslation(singular, plural, count);
}
#endif /* ENABLE_I18N_SUPPORT */

static int
updateProperty (
  char **property, const char *value, const char *defaultValue,
  int (*setter) (const char *value)
) {
  releaseMessagesData();

  if (!(value && *value)) value = defaultValue;
  char *copy = strdup(value);

  if (copy) {
    if (!setter || setter(value)) {
      if (*property) free(*property);
      *property = copy;
      return 1;
    }

    free(copy);
  } else {
    logMallocError();
  }

  return 0;
}

int
setMessagesDirectory (const char *directory) {
  return updateProperty(&localeDirectory, directory, LOCALE_DIRECTORY, setDirectory);
}

int
setMessagesLocale (const char *specifier) {
  return updateProperty(&localeSpecifier, specifier, "C.UTF-8", NULL);
}

int
setMessagesDomain (const char *name) {
  return updateProperty(&domainName, name, PACKAGE_TARNAME, setDomain);
}

void
ensureAllMessagesProperties (void) {
  if (!localeSpecifier) {
    setMessagesLocale(setlocale(LC_MESSAGES, ""));
  }

  if (!domainName) setMessagesDomain(NULL);
  if (!localeDirectory) setMessagesDirectory(NULL);
}
