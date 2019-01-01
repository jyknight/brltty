/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2018 by The BRLTTY Developers.
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

/* ViaVoice/speech.c */

#include "prologue.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <eci.h>

#include "log.h"
#include "parse.h"

typedef enum {
   PARM_IniFile,
   PARM_SampleRate,
   PARM_AbbreviationMode,
   PARM_NumberMode,
   PARM_SynthMode,
   PARM_TextMode,
   PARM_Language,
   PARM_Voice,
   PARM_Gender,
   PARM_Breathiness,
   PARM_HeadSize,
   PARM_PitchBaseline,
   PARM_PitchFluctuation,
   PARM_Roughness
} DriverParameter;
#define SPKPARMS "inifile", "samplerate", "abbreviationmode", "numbermode", "synthmode", "textmode", "language", "voice", "gender", "breathiness", "headsize", "pitchbaseline", "pitchfluctuation", "roughness"

#include "spk_driver.h"
#include "speech.h"

#define MAXIMUM_SAMPLES 0X800

static ECIHand eciHandle = NULL_ECI_HAND;
static short *pcmBuffer = NULL;
static char *pcmCommand = NULL;
static FILE *pcmStream = NULL;

static int useSSML;
static int currentUnits;
static int currentInputType;

static char *sayBuffer = NULL;
static size_t saySize;

typedef int MapFunction (int index);

static const char *sampleRates[] = {"8000", "11025", "22050", NULL};
static const char *abbreviationModes[] = {"on", "off", NULL};
static const char *numberModes[] = {"word", "year", NULL};
static const char *synthModes[] = {"sentence", "none", NULL};
static const char *textModes[] = {"talk", "spell", "literal", "phonetic", NULL};
static const char *voices[] = {"", "dad", "mom", "child", "", "", "", "grandma", "grandpa", NULL};
static const char *genders[] = {"male", "female", NULL};

typedef struct {
   const char *name; // must be first
   const char *language;
   const char *territory;
   const char *encoding;
   int identifier;
} LanguageEntry;

static const LanguageEntry languages[] = {
   {  .identifier = eciGeneralAmericanEnglish,
      .name = "American-English",
      .language = "en",
      .territory = "US",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciBritishEnglish,
      .name = "British-English",
      .language = "en",
      .territory = "GB",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciCastilianSpanish,
      .name = "Castilian-Spanish",
      .language = "es",
      .territory = "ES",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciMexicanSpanish,
      .name = "Mexican-Spanish",
      .language = "es",
      .territory = "MX",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardFrench,
      .name = "Standard-French",
      .language = "fr",
      .territory = "FR",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciCanadianFrench,
      .name = "Canadian-French",
      .language = "fr",
      .territory = "CA",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardGerman,
      .name = "Standard-German",
      .language = "de",
      .territory = "DE",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardItalian,
      .name = "Standard-Italian",
      .language = "it",
      .territory = "IT",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciMandarinChinese,
      .name = "Standard-Mandarin",
      .language = "zh",
      .territory = "CN",
      .encoding = "GBK"
   },

   {  .identifier = eciMandarinChineseGB,
      .name = "Standard-Mandarin-GB",
      .language = "zh",
      .territory = "CN_GB",
      .encoding = "GBK"
   },

   {  .identifier = eciMandarinChinesePinYin,
      .name = "Standard-Mandarin-PinYin",
      .language = "zh",
      .territory = "CN_PinYin",
      .encoding = "GBK"
   },

   {  .identifier = eciMandarinChineseUCS,
      .name = "Standard-Mandarin-UCS",
      .language = "zh",
      .territory = "CN_UCS",
      .encoding = "UCS2"
   },

   {  .identifier = eciTaiwaneseMandarin,
      .name = "Taiwanese-Mandarin",
      .language = "zh",
      .territory = "TW",
      .encoding = "BIG5"
   },

   {  .identifier = eciTaiwaneseMandarinBig5,
      .name = "Taiwanese-Mandarin-Big5",
      .language = "zh",
      .territory = "TW_Big5",
      .encoding = "BIG5"
   },

   {  .identifier = eciTaiwaneseMandarinZhuYin,
      .name = "Taiwanese-Mandarin-ZhuYin",
      .language = "zh",
      .territory = "TW_ZhuYin",
      .encoding = "BIG5"
   },

   {  .identifier = eciTaiwaneseMandarinPinYin,
      .name = "Taiwanese-Mandarin-PinYin",
      .language = "zh",
      .territory = "TW_PinYin",
      .encoding = "BIG5"
   },

   {  .identifier = eciTaiwaneseMandarinUCS,
      .name = "Taiwanese-Mandarin-UCS",
      .language = "zh",
      .territory = "TW_UCS",
      .encoding = "UCS2"
   },

   {  .identifier = eciBrazilianPortuguese,
      .name = "Brazilian-Portuguese",
      .language = "pt",
      .territory = "BR",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardJapanese,
      .name = "Standard-Japanese",
      .language = "ja",
      .territory = "JP",
      .encoding = "SJIS"
   },

   {  .identifier = eciStandardJapaneseSJIS,
      .name = "Standard-Japanese-SJIS",
      .language = "ja",
      .territory = "JP_SJIS",
      .encoding = "SJIS"
   },

   {  .identifier = eciStandardJapaneseUCS,
      .name = "Standard-Japanese-UCS",
      .language = "ja",
      .territory = "JP_UCS",
      .encoding = "UCS2"
   },

   {  .identifier = eciStandardFinnish,
      .name = "Standard-Finnish",
      .language = "fi",
      .territory = "FI",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardKorean,
      .name = "Standard-Korean",
      .language = "ko",
      .territory = "KR",
      .encoding = "UHC"
   },

   {  .identifier = eciStandardKoreanUHC,
      .name = "Standard-Korean-UHC",
      .language = "ko",
      .territory = "KR_UHC",
      .encoding = "UHC"
   },

   {  .identifier = eciStandardKoreanUCS,
      .name = "Standard-Korean-UCS",
      .language = "ko",
      .territory = "KR_UCS",
      .encoding = "UCS2"
   },

   {  .identifier = eciStandardCantonese,
      .name = "Standard-Cantonese",
      .language = "zh",
      .territory = "HK",
      .encoding = "GBK"
   },

   {  .identifier = eciStandardCantoneseGB,
      .name = "Standard-Cantonese-GB",
      .language = "zh",
      .territory = "HK_GB",
      .encoding = "GBK"
   },

   {  .identifier = eciStandardCantoneseUCS,
      .name = "Standard-Cantonese-UCS",
      .language = "zh",
      .territory = "HK_UCS",
      .encoding = "UCS2"
   },

   {  .identifier = eciHongKongCantonese,
      .name = "HongKong-Cantonese",
      .language = "zh",
      .territory = "HK",
      .encoding = "BIG5"
   },

   {  .identifier = eciHongKongCantoneseBig5,
      .name = "HongKong-Cantonese-Big5",
      .language = "zh",
      .territory = "HK_BIG5",
      .encoding = "BIG5"
   },

   {  .identifier = eciHongKongCantoneseUCS,
      .name = "HongKong-Cantonese-UCS",
      .language = "zh",
      .territory = "HK_UCS",
      .encoding = "UCS-2"
   },

   {  .identifier = eciStandardDutch,
      .name = "Standard-Dutch",
      .language = "nl",
      .territory = "NL",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardNorwegian,
      .name = "Standard-Norwegian",
      .language = "no",
      .territory = "NO",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardSwedish,
      .name = "Standard-Swedish",
      .language = "sv",
      .territory = "SE",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardDanish,
      .name = "Standard-Danish",
      .language = "da",
      .territory = "DK",
      .encoding = "ISO-8859-1"
   },

   {  .identifier = eciStandardThai,
      .name = "Standard-Thai",
      .language = "th",
      .territory = "TH",
      .encoding = "TIS-620"
   },

   {  .identifier = eciStandardThaiTIS,
      .name = "Standard-Thai-TIS",
      .language = "th",
      .territory = "TH_TIS",
      .encoding = "TIS-620"
   },

   {  .identifier = NODEFINEDCODESET  }
};

static int
mapLanguage (int index) {
   return languages[index].identifier;
}

static void
reportError (ECIHand eci, const char *routine) {
   int status = eciProgStatus(eci);
   char message[100];
   eciErrorMessage(eci, message);
   logMessage(LOG_ERR, "%s error %4.4X: %s", routine, status, message);
}

static void
reportParameter (const char *description, int setting, const char *const *choices, MapFunction *map) {
   char buffer[0X10];
   const char *value = buffer;

   if (setting == -1) {
      value = "unknown";
   } else if (choices) {
      int choice = 0;

      while (choices[choice]) {
	 if (setting == (map? map(choice): choice)) {
	    value = choices[choice];
	    break;
	 }

         choice += 1;
      }
   }

   if (value == buffer) snprintf(buffer, sizeof(buffer), "%d", setting);
   logMessage(LOG_DEBUG, "ViaVoice Parameter: %s = %s", description, value);
}

static void
reportGeneralParameter (ECIHand eci, const char *description, enum ECIParam parameter, int setting, const char *const *choices, MapFunction *map) {
   if (parameter != eciNumParams) setting = eciGetParam(eci, parameter);
   reportParameter(description, setting, choices, map);
}

static int
setGeneralParameter (ECIHand eci, const char *description, enum ECIParam parameter, int setting) {
   if (parameter == eciNumParams) {
      logMessage(LOG_CATEGORY(SPEECH_DRIVER), "copy voice: %d", setting);
      int ok = eciCopyVoice(eci, setting, 0);
      if (!ok) reportError(eci, "eciCopyVoice");
      return ok;
   }

   logMessage(LOG_CATEGORY(SPEECH_DRIVER), "set general parameter: %s: %d=%d", description, parameter, setting);
   return eciSetParam(eci, parameter, setting) >= 0;
}

static int
choiceGeneralParameter (ECIHand eci, const char *description, const char *value, enum ECIParam parameter, const void *choices, size_t size, MapFunction *map) {
   int ok = !*value;
   int assume = 1;

   if (!ok) {
      unsigned int setting;

      if (validateChoiceEx(&setting, value, choices, size)) {
	 if (map) setting = map(setting);

         if (setGeneralParameter(eci, description, parameter, setting)) {
	    ok = 1;
	    assume = setting;
         } else {
            logMessage(LOG_WARNING, "%s not supported: %s", description, value);
	 }
      } else {
        logMessage(LOG_WARNING, "invalid %s setting: %s", description, value);
      }
   }

   reportGeneralParameter(eci, description, parameter, assume, choices, map);
   return ok;
}

static int
setUnits (ECIHand eci, int newUnits) {
   if (newUnits != currentUnits) {
      if (!setGeneralParameter(eci, "real world units", eciRealWorldUnits, newUnits)) return 0;
      currentUnits = newUnits;
   }

   return 1;
}

static int
useInternalUnits (ECIHand eci) {
   return setUnits(eci, 0);
}

static int
useExternalUnits (ECIHand eci) {
   return setUnits(eci, 1);
}

static int
useParameterUnits (ECIHand eci, enum ECIVoiceParam parameter) {
   switch (parameter) {
      case eciVolume:
         if (!useInternalUnits(eci)) return 0;
         break;

      case eciPitchBaseline:
      case eciSpeed:
         if (!useExternalUnits(eci)) return 0;
         break;

      default:
         break;
   }

   return 1;
}

static int
getVoiceParameter (ECIHand eci, enum ECIVoiceParam parameter) {
   if (!useParameterUnits(eci, parameter)) return 0;
   return eciGetVoiceParam(eci, 0, parameter);
}

static void
reportVoiceParameter (ECIHand eci, const char *description, enum ECIVoiceParam parameter, const char *const *choices, MapFunction *map) {
   reportParameter(description, getVoiceParameter(eci, parameter), choices, map);
}

static int
setVoiceParameter (ECIHand eci, const char *description, enum ECIVoiceParam parameter, int setting) {
   if (!useParameterUnits(eci, parameter)) return 0;
   logMessage(LOG_CATEGORY(SPEECH_DRIVER), "set voice parameter: %s: %d=%d", description, parameter, setting);
   return eciSetVoiceParam(eci, 0, parameter, setting) >= 0;
}

static int
choiceVoiceParameter (ECIHand eci, const char *description, const char *value, enum ECIVoiceParam parameter, const char *const *choices, MapFunction *map) {
   int ok = !*value;

   if (!ok) {
      unsigned int setting;

      if (validateChoice(&setting, value, choices)) {
	 if (map) setting = map(setting);

         if (setVoiceParameter(eci, description, parameter, setting)) {
            ok = 1;
         } else {
            logMessage(LOG_WARNING, "%s not supported: %s", description, value);
         }
      } else {
        logMessage(LOG_WARNING, "invalid %s setting: %s", description, value);
      }
   }

   reportVoiceParameter(eci, description, parameter, choices, map);
   return ok;
}

static int
rangeVoiceParameter (ECIHand eci, const char *description, const char *value, enum ECIVoiceParam parameter, int minimum, int maximum) {
   int ok = 0;

   if (*value) {
      int setting;

      if (validateInteger(&setting, value, &minimum, &maximum)) {
         if (setVoiceParameter(eci, description, parameter, setting)) {
	    ok = 1;
	 }
      } else {
        logMessage(LOG_WARNING, "invalid %s setting: %s", description, value);
      }
   }

   reportVoiceParameter(eci, description, parameter, NULL, NULL);
   return ok;
}

static void
spk_setVolume (volatile SpeechSynthesizer *spk, unsigned char setting) {
   setVoiceParameter(eciHandle, "volume", eciVolume, getIntegerSpeechVolume(setting, 100));
}

static void
spk_setRate (volatile SpeechSynthesizer *spk, unsigned char setting) {
   setVoiceParameter(eciHandle, "rate", eciSpeed, (int)(getFloatSpeechRate(setting) * 210.0));
}

static int
pcmMakeCommand (void) {
   int rate = eciGetParam(eciHandle, eciSampleRate);
   char buffer[0X100];

   snprintf(
      buffer, sizeof(buffer),
      "sox -q -t raw -c 1 -b %" PRIsize " -e signed-integer -r %s - -d",
      (sizeof(*pcmBuffer) * 8), sampleRates[rate]
   );

   logMessage(LOG_CATEGORY(SPEECH_DRIVER), "PCM command: %s", buffer);
   pcmCommand = strdup(buffer);
   if (pcmCommand) return 1;
   logMallocError();
   return 0;
}

static int
pcmOpenStream (void) {
   if (!pcmStream) {
      if (!pcmCommand) {
         if (!pcmMakeCommand()) {
            return 0;
         }
      }

      if (!(pcmStream = popen(pcmCommand, "w"))) {
         logMessage(LOG_WARNING, "can't start command: %s", strerror(errno));
         return 0;
      }

      setvbuf(pcmStream, NULL, _IONBF, 0);
   }

   return 1;
}

static void
pcmCloseStream (void) {
   if (pcmStream) {
      pclose(pcmStream);
      pcmStream = NULL;
   }
}

static enum ECICallbackReturn
clientCallback (ECIHand eci, enum ECIMessage message, long parameter, void *data) {
   volatile SpeechSynthesizer *spk = data;

   switch (message) {
      case eciWaveformBuffer:
         logMessage(LOG_CATEGORY(SPEECH_DRIVER), "write samples: %ld", parameter);
         fwrite(pcmBuffer, sizeof(*pcmBuffer), parameter, pcmStream);
         if (ferror(pcmStream)) return eciDataAbort;
         break;

      case eciIndexReply:
         logMessage(LOG_CATEGORY(SPEECH_DRIVER), "index reply: %ld", parameter);
         tellSpeechLocation(spk, parameter);
         break;

      default:
         break;
   }

   return eciDataProcessed;
}

static int
setInputType (ECIHand eci, int newInputType) {
   if (newInputType != currentInputType) {
      if (!setGeneralParameter(eci, "input type", eciInputType, newInputType)) return 0;
      currentInputType = newInputType;
   }

   return 1;
}

static int
disableAnnotations (ECIHand eci) {
   return setInputType(eci, 0);
}

static int
enableAnnotations (ECIHand eci) {
   return setInputType(eci, 1);
}

static int
addText (ECIHand eci, const char *text) {
   logMessage(LOG_CATEGORY(SPEECH_DRIVER), "add text: \"%s\"", text);
   if (eciAddText(eci, text)) return 1;
   reportError(eci, "eciAddText");
   return 0;
}

static int
writeAnnotation (ECIHand eci, const char *annotation) {
   if (!enableAnnotations(eci)) return 0;

   char text[0X100];
   snprintf(text, sizeof(text), " `%s ", annotation);
   return addText(eci, text);
}

#ifdef HAVE_ICONV_H
#include <iconv.h>
#define ICONV_NULL ((iconv_t)-1)
static iconv_t textConverter = ICONV_NULL;

static int
prepareTextConversion (ECIHand eci) {
   textConverter = ICONV_NULL;

   int identifier = eciGetParam(eci, eciLanguageDialect);
   const LanguageEntry *entry = languages;

   while (entry->name) {
      if (entry->identifier == identifier) {
         iconv_t *converter = iconv_open(entry->encoding, "UTF-8");

         if (converter == ICONV_NULL) {
            logMessage(LOG_WARNING, "character encoding not supported: %s: %s", entry->encoding, strerror(errno));
            return 0;
         }

         textConverter = converter;
         return 1;
      }

      entry += 1;
   }

   logMessage(LOG_WARNING, "language identifier not defined: 0X%08X", identifier);
   return 0;
}

#else /* HAVE_ICONV_H */
#warning iconv is not available
#endif /* HAVE_ICONV_H */

static int
ensureSayBuffer (int size) {
   if (size > saySize) {
      size |= 0XFF;
      size += 1;
      char *newBuffer = malloc(size);

      if (!newBuffer) {
         logSystemError("speech buffer allocation");
	 return 0;
      }

      free(sayBuffer);
      sayBuffer = newBuffer;
      saySize = size;
   }

   return 1;
}

static int
addCharacters (ECIHand eci, const unsigned char *buffer, int from, int to) {
   int length = to - from;
   if (!length) return 1;

   if (!ensureSayBuffer(length+1)) return 0;
   memcpy(sayBuffer, &buffer[from], length);
   sayBuffer[length] = 0;
   return addText(eci, sayBuffer);
}

static int
addSegment (ECIHand eci, const unsigned char *buffer, int from, int to, const int *indexMap) {
   if (useSSML) {
      for (int index=from; index<to; index+=1) {
         const char *entity = NULL;

         switch (buffer[index]) {
            case '<':
               entity = "lt";
               break;

            case '>':
               entity = "gt";
               break;

            case '&':
               entity = "amp";
               break;

            case '"':
               entity = "quot";
               break;

            case '\'':
               entity = "apos";
               break;

            default:
               continue;
         }

         if (!addCharacters(eci, buffer, from, index)) return 0;
         from = index + 1;

         size_t length = strlen(entity);
         char text[1 + length + 2];
         snprintf(text, sizeof(text), "&%s;", entity);
         if (!addText(eci, text)) return 0;
      }

      if (!addCharacters(eci, buffer, from, to)) return 0;
   } else {
#ifdef ICONV_NULL
      char *inputStart = (char *)&buffer[from];
      size_t inputLeft = to - from;
      size_t outputLeft = inputLeft * 10;
      char outputBuffer[outputLeft];
      char *outputStart = outputBuffer;
      int result = iconv(textConverter, &inputStart, &inputLeft, &outputStart, &outputLeft);

      if (result == -1) {
         logSystemError("iconv");
         return 0;
      }

      if (!addCharacters(eci, (unsigned char *)outputBuffer, 0, (outputStart - outputBuffer))) return 0;
#else /* ICONV_NULL */
      if (!addCharacters(eci, buffer, from, to)) return 0;
#endif /* ICONV_NULL */
   }

   int index = indexMap[to];
   logMessage(LOG_CATEGORY(SPEECH_DRIVER), "insert index: %d", index);
   if (eciInsertIndex(eci, index)) return 1;
   reportError(eci, "eciInsertIndex");
   return 0;
}

static int
addSegments (ECIHand eci, const unsigned char *buffer, size_t length, const int *indexMap) {
   if (useSSML && !addText(eciHandle, "<speak>")) return 0;

   int onSpace = -1;
   int from = 0;
   int to;

   for (to=0; to<length; to+=1) {
      int isSpace = isspace(buffer[to])? 1: 0;

      if (isSpace != onSpace) {
         onSpace = isSpace;

         if (to > from) {
            if (!addSegment(eci, buffer, from, to, indexMap)) return 0;
            from = to;
         }
      }
   }

   if (!addSegment(eci, buffer, from, to, indexMap)) return 0;
   if (useSSML && !addText(eciHandle, "</speak>")) return 0;
   return 1;
}

static void
spk_say (volatile SpeechSynthesizer *spk, const unsigned char *buffer, size_t length, size_t count, const unsigned char *attributes) {
   int ok = 0;
   int indexMap[length + 1];

   {
      int from = 0;
      int to = 0;

      while (from < length) {
         char character = buffer[from];
         indexMap[from++] = ((character & 0X80) && !(character & 0X40))? -1: to++;
      }

      indexMap[from] = to;
   }

   if (pcmOpenStream()) {
      if (addSegments(eciHandle, buffer, length, indexMap)) {
         logMessage(LOG_CATEGORY(SPEECH_DRIVER), "synthesize");

         if (eciSynthesize(eciHandle)) {
            logMessage(LOG_CATEGORY(SPEECH_DRIVER), "synchronize");

            if (eciSynchronize(eciHandle)) {
               logMessage(LOG_CATEGORY(SPEECH_DRIVER), "finished");
               tellSpeechFinished(spk);
               ok = 1;
            } else {
               reportError(eciHandle, "eciSynchronize");
            }
         } else {
            reportError(eciHandle, "eciSynthesize");
         }
      }

      if (!ok) eciStop(eciHandle);
      pcmCloseStream();
   }
}

static void
spk_mute (volatile SpeechSynthesizer *spk) {
   if (eciStop(eciHandle)) {
   } else {
      reportError(eciHandle, "eciStop");
   }
}

static int
setIni (const char *path) {
   const char *variable = INI_VARIABLE;
   logMessage(LOG_DEBUG, "ViaVoice Ini Variable: %s", variable);

   if (!*path) {
      const char *value = getenv(variable);
      if (value) {
         path = value;
	 goto isSet;
      }
      value = INI_DEFAULT;
   }
   if (setenv(variable, path, 1) == -1) {
      logSystemError("setenv");
      return 0;
   }
isSet:

   logMessage(LOG_INFO, "ViaVoice Ini File: %s", path);
   return 1;
}

static void
setParameters (ECIHand eci, char **parameters) {
   currentUnits = eciGetParam(eci, eciRealWorldUnits);
   currentInputType = eciGetParam(eci, eciInputType);

   choiceGeneralParameter(eci, "sample rate", parameters[PARM_SampleRate], eciSampleRate, sampleRates, sizeof(*sampleRates), NULL);
   choiceGeneralParameter(eci, "abbreviation mode", parameters[PARM_AbbreviationMode], eciDictionary, abbreviationModes, sizeof(*abbreviationModes), NULL);
   choiceGeneralParameter(eci, "number mode", parameters[PARM_NumberMode], eciNumberMode, numberModes, sizeof(*numberModes), NULL);
   choiceGeneralParameter(eci, "synth mode", parameters[PARM_SynthMode], eciSynthMode, synthModes, sizeof(*synthModes), NULL);
   choiceGeneralParameter(eci, "text mode", parameters[PARM_TextMode], eciTextMode, textModes, sizeof(*textModes), NULL);
   choiceGeneralParameter(eci, "language", parameters[PARM_Language], eciLanguageDialect, languages, sizeof(*languages), mapLanguage);
   choiceGeneralParameter(eci, "voice", parameters[PARM_Voice], eciNumParams, voices, sizeof(*voices), NULL);

   choiceVoiceParameter(eci, "gender", parameters[PARM_Gender], eciGender, genders, NULL);
   rangeVoiceParameter(eci, "breathiness", parameters[PARM_Breathiness], eciBreathiness, 0, 100);
   rangeVoiceParameter(eci, "head size", parameters[PARM_HeadSize], eciHeadSize, 0, 100);
   rangeVoiceParameter(eci, "pitch baseline", parameters[PARM_PitchBaseline], eciPitchBaseline, 0, 100);
   rangeVoiceParameter(eci, "pitch fluctuation", parameters[PARM_PitchFluctuation], eciPitchFluctuation, 0, 100);
   rangeVoiceParameter(eci, "roughness", parameters[PARM_Roughness], eciRoughness, 0, 100);

#ifdef ICONV_NULL
   useSSML = !prepareTextConversion(eci);
#else /* ICONV_NULL */
   useSSML = 1;
#endif /* ICONV_NULL */
}

static void
writeAnnotations (ECIHand eci) {
   if (useSSML) {
      writeAnnotation(eciHandle, "gfa1"); // enable SSML
      writeAnnotation(eciHandle, "gfa2");
   }

   disableAnnotations(eciHandle);
}

static int
spk_construct (volatile SpeechSynthesizer *spk, char **parameters) {
   spk->setVolume = spk_setVolume;
   spk->setRate = spk_setRate;

   sayBuffer = NULL;
   saySize = 0;

   if (setIni(parameters[PARM_IniFile])) {
      {
         char version[0X80];
         eciVersion(version);
         logMessage(LOG_INFO, "ViaVoice Engine: version %s", version);
      }

      if ((eciHandle = eciNew()) != NULL_ECI_HAND) {
         eciRegisterCallback(eciHandle, clientCallback, (void *)spk);

         if ((pcmBuffer = calloc(MAXIMUM_SAMPLES, sizeof(*pcmBuffer)))) {
            if (eciSetOutputBuffer(eciHandle, MAXIMUM_SAMPLES, pcmBuffer)) {
               setParameters(eciHandle, parameters);
               writeAnnotations(eciHandle);
               return 1;
            } else {
               reportError(eciHandle, "eciSetOutputBuffer");
            }

            free(pcmBuffer);
            pcmBuffer = NULL;
         } else {
            logMallocError();
         }

         eciDelete(eciHandle);
         eciHandle = NULL_ECI_HAND;
      } else {
         logMessage(LOG_ERR, "ViaVoice initialization error");
      }
   }

   return 0;
}

static void
spk_destruct (volatile SpeechSynthesizer *spk) {
   if (eciHandle) {
      eciDelete(eciHandle);
      eciHandle = NULL_ECI_HAND;
   }

   pcmCloseStream();

   if (pcmBuffer) {
      free(pcmBuffer);
      pcmBuffer = NULL;
   }

   if (pcmCommand) {
      free(pcmCommand);
      pcmCommand = NULL;
   }

#ifdef ICONV_NULL
   if (textConverter != ICONV_NULL) {
      iconv_close(textConverter);
      textConverter = ICONV_NULL;
   }
#endif /* ICONV_NULL */
}
