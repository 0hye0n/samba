/*
 * Copyright (c) 1995, 1996, 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the Kungliga Tekniska
 *      H�gskolan and its contributors.
 * 
 * 4. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "otp_locl.h"

RCSID("$Id$");

static void
usage (void)
{
  fprintf(stderr,
	  "Usage: %s [-e] [-h] [-n count] [-f alg] num seed\n",
	  __progname);
  exit (1);
}

static int
print (int argc,
       char **argv,
       int count,
       OtpAlgorithm *alg,
       void (*print_fn)(OtpKey, char *, size_t))
{
  char pw[64];
  OtpKey key;
  int n;
  int i;
  char *seed;

  if (argc != 2)
    usage ();
  n = atoi(argv[0]);
  seed = argv[1];
  if (des_read_pw_string (pw, sizeof(pw), "Pass-phrase: ", 0))
    return 1;
  alg->init (key, pw, seed);
  for (i = 0; i < n; ++i) {
    char s[64];

    alg->next (key);
    if (i >= n - count) {
      (*print_fn)(key, s, sizeof(s));
      printf ("%d: %s\n", i + 1, s);
    }
  }
  return 0;
}

int
main (int argc, char **argv)
{
  int c;
  int count = 10;
  int hexp = 0;
  int extendedp = 0;
  void (*fn)(OtpKey, char *, size_t);
  OtpAlgorithm *alg = otp_find_alg (OTP_ALG_DEFAULT);

  set_progname (argv[0]);

  while ((c = getopt (argc, argv, "ehn:f:")) != EOF)
    switch (c) {
    case 'e' :
      extendedp = 1;
      break;
    case 'n' :
      count = atoi (optarg);
      break;
    case 'h' :
      hexp = 1;
      break;
    case 'f' :
      alg = otp_find_alg (optarg);
      if (alg == NULL)
	errx(1, "Unknown algorithm: %s", optarg);
      break;
    default :
      usage ();
      break;
    }
  
  argc -= optind;
  argv += optind;

  if (hexp)
    if (extendedp)
      fn = otp_print_hex_extended;
    else
      fn = otp_print_hex;
  else
    if (extendedp)
      fn = otp_print_stddict_extended;
    else
      fn = otp_print_stddict;

  return print (argc, argv, count, alg, fn);
}
