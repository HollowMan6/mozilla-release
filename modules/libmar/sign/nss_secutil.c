/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is code copied from secutil and secpwd.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian R. Bondy <netzen@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/* With the exception of GetPasswordString, this file was
   copied from NSS's cmd/lib/secutil.c hg revision 8f011395145e */

#include "nss_secutil.h"

#include "prprf.h"
#ifdef XP_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

static char consoleName[] =  {
#ifdef XP_UNIX
  "/dev/tty"
#else
#ifdef XP_OS2
  "\\DEV\\CON"
#else
  "CON:"
#endif
#endif
};

#if defined(_WINDOWS)
static char * quiet_fgets (char *buf, int length, FILE *input)
{
  int c;
  char *end = buf;

  /* fflush (input); */
  memset (buf, 0, length);

  if (!isatty(fileno(input))) {
    return fgets(buf,length,input);
  }

  while (1)
  {
#if defined (_WIN32_WCE)
    c = getchar();	/* gets a character from stdin */
#else
    c = getch();	/* getch gets a character from the console */
#endif
    if (c == '\b')
    {
      if (end > buf)
        end--;
    }

    else if (--length > 0)
      *end++ = c;

    if (!c || c == '\n' || c == '\r')
      break;
  }

  return buf;
}
#endif

char *
GetPasswordString(void *arg, char *prompt)
{
  FILE *input = stdin;
  char phrase[200] = {'\0'};
  int isInputTerminal = isatty(fileno(stdin));

#ifndef _WINDOWS
  if (isInputTerminal) {
    input = fopen(consoleName, "r");
    if (input == NULL) {
      fprintf(stderr, "Error opening input terminal for read\n");
      return NULL;
    }
  }
#endif 

  if (isInputTerminal) {
    fprintf(stdout, "Please enter your password:\n");
    fflush(stdout);
  }

  QUIET_FGETS (phrase, sizeof(phrase), input);

  if (isInputTerminal) {
    fprintf(stdout, "\n");
  }

#ifndef _WINDOWS
  if (isInputTerminal) {
    fclose(input);
  }
#endif

  /* Strip off the newlines if present */
  if (phrase[PORT_Strlen(phrase)-1] == '\n' || 
      phrase[PORT_Strlen(phrase)-1] == '\r') {
    phrase[PORT_Strlen(phrase)-1] = 0;
  }
  return (char*) PORT_Strdup(phrase);
}

char *
SECU_FilePasswd(PK11SlotInfo *slot, PRBool retry, void *arg)
{
  char* phrases, *phrase;
  PRFileDesc *fd;
  PRInt32 nb;
  char *pwFile = arg;
  int i;
  const long maxPwdFileSize = 4096;
  char* tokenName = NULL;
  int tokenLen = 0;

  if (!pwFile)
    return 0;

  if (retry) {
    return 0;  /* no good retrying - the files contents will be the same */
  }

  phrases = PORT_ZAlloc(maxPwdFileSize);

  if (!phrases) {
    return 0; /* out of memory */
  }

  fd = PR_Open(pwFile, PR_RDONLY, 0);
  if (!fd) {
    fprintf(stderr, "No password file \"%s\" exists.\n", pwFile);
    PORT_Free(phrases);
    return NULL;
  }

  nb = PR_Read(fd, phrases, maxPwdFileSize);

  PR_Close(fd);

  if (nb == 0) {
    fprintf(stderr,"password file contains no data\n");
    PORT_Free(phrases);
    return NULL;
  }

  if (slot) {
    tokenName = PK11_GetTokenName(slot);
    if (tokenName) {
      tokenLen = PORT_Strlen(tokenName);
    }
  }
  i = 0;
  do
  {
    int startphrase = i;
    int phraseLen;

    /* handle the Windows EOL case */
    while (phrases[i] != '\r' && phrases[i] != '\n' && i < nb) i++;
    /* terminate passphrase */
    phrases[i++] = '\0';
    /* clean up any EOL before the start of the next passphrase */
    while ( (i<nb) && (phrases[i] == '\r' || phrases[i] == '\n')) {
      phrases[i++] = '\0';
    }
    /* now analyze the current passphrase */
    phrase = &phrases[startphrase];
    if (!tokenName)
      break;
    if (PORT_Strncmp(phrase, tokenName, tokenLen)) continue;
    phraseLen = PORT_Strlen(phrase);
    if (phraseLen < (tokenLen+1)) continue;
    if (phrase[tokenLen] != ':') continue;
    phrase = &phrase[tokenLen+1];
    break;

  } while (i<nb);

  phrase = PORT_Strdup((char*)phrase);
  PORT_Free(phrases);
  return phrase;
}

char *
SECU_GetModulePassword(PK11SlotInfo *slot, PRBool retry, void *arg) 
{
    char prompt[255];
    secuPWData *pwdata = (secuPWData *)arg;
    secuPWData pwnull = { PW_NONE, 0 };
    secuPWData pwxtrn = { PW_EXTERNAL, "external" };
    char *pw;

    if (pwdata == NULL)
  pwdata = &pwnull;

    if (PK11_ProtectedAuthenticationPath(slot)) {
  pwdata = &pwxtrn;
    }
    if (retry && pwdata->source != PW_NONE) {
  PR_fprintf(PR_STDERR, "Incorrect password/PIN entered.\n");
      return NULL;
    }

    switch (pwdata->source) {
    case PW_NONE:
  sprintf(prompt, "Enter Password or Pin for \"%s\":",
                   PK11_GetTokenName(slot));
  return GetPasswordString(NULL, prompt);
    case PW_FROMFILE:
  /* Instead of opening and closing the file every time, get the pw
   * once, then keep it in memory (duh).
   */
  pw = SECU_FilePasswd(slot, retry, pwdata->data);
  pwdata->source = PW_PLAINTEXT;
  pwdata->data = PL_strdup(pw);
  /* it's already been dup'ed */
  return pw;
    case PW_EXTERNAL:
  sprintf(prompt, 
          "Press Enter, then enter PIN for \"%s\" on external device.\n",
    PK11_GetTokenName(slot));
  (void) GetPasswordString(NULL, prompt);
      /* Fall Through */
    case PW_PLAINTEXT:
  return PL_strdup(pwdata->data);
    default:
  break;
    }

    PR_fprintf(PR_STDERR, "Password check failed:  No password found.\n");
    return NULL;
}
