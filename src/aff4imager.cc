/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

/*
  This is the command line tool to manager aff4 image volumes and acquire
  images.
*/

#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include "libaff4.h"
#include "aff4_simple.h"


void print_help(struct option *opts) {
  struct option *i;

  printf("The following options are supported\n");

  for(i=opts; i->name; i++) {
    char *description = (char *)i->name + strlen(i->name) + 1;
    char short_opt[10];
    char *prefix="\t";

    if(description[0]=='*') {
      prefix="\n";
      description++;
    };

    if(i->val)
      snprintf(short_opt, sizeof(short_opt), ", -%c", i->val);
    else
      short_opt[0]=0;

    if(i->has_arg) {
      printf("%s--%s%s [ARG]\t%s\n", prefix, i->name, short_opt, description);
    } else {
      printf("%s--%s%s\t\t%s\n", prefix,i->name, short_opt, description);
    };
  };
};


char *generate_short_optargs(struct option *opts) {
  static char short_opts[100];
  int j,i;

  for(i=0, j=0; i<sizeof(short_opts) && opts[j].name; j++) {
    if(opts[j].val) {
      short_opts[i]=opts[j].val;
      i++;
      if(opts[j].has_arg) {
	short_opts[i]=':';
	i++;
      };
    };
  };

  short_opts[i]=0;

  return short_opts;
};


int main(int argc, char **argv)
{
  int c;
  char mode=0;
  char *output_file = NULL;
  char *stream_name = NULL;
  int chunks_per_segment = 0;
  char *extract = NULL;
  char *driver;
  char *cert = NULL;
  char *key_file = NULL;
  int verify = 0;
  size_t max_size=0;

  while (1) {
    int option_index = 0;

    // Note that we use an extension to long_options to allow the
    // helpful descriptions to be included with the long names. This
    // keeps everything well synchronised in the same place.
    static struct option long_options[] = {

      {"help\0"
       "*This message", 0, 0, 'h'},
      {"verbose\0"
       "*Verbose (can be specified more than once)", 0, 0, 'v'},

      {"image\0"
       "*Imaging mode (Image each argv as a new stream)", 0, 0, 'i'},

      {"map\0"
       "*Map only (create a map object concatenating all the images)", 0, 0, 'm'},

      {"driver\0"
       "Which driver to use - 'directory' or 'volume' (Zip archive, default)", 1, 0, 'd'},

      {"output\0"
       "Create the output volume on this file or URL (using webdav)", 1, 0, 'o'},

      {"chunks_per_segment\0"
       "How many chunks in each segment of the image (default 2048)", 1, 0, 0},

      {"max_size\0"
       "When a volume exceeds this size, a new volume is created "
       "(default 0-unlimited)", 1, 0, 0},

      {"stream\0"
       "If specified a link will be added with this name to the new stream",
       1, 0, 's'},

      {"cert\0"
       "Certificate to use for signing", 1, 0, 'c'},

      {"key\0"
       "Private key in PEM format (needed for signing)", 1, 0, 'k'},

      {"passphrase\0"
       "Encrypt the volume using this passphrase", 1, 0, 'p'},

      {"info\0"
       "*Information mode (print information on all objects in this volume)",
       0, 0, 'I'},

      {"load\0"
       "Open this file and populate the resolver (can be provided multiple times)",
       1, 0, 'l'},

      {"no_autoload\0"
       "Do not automatically load volumes (affects subsequent --load)", 0, 0, 0},

      {"extract\0"
       "*Extract mode (dump the content of stream)", 1, 0, 'e'},

      {"verify\0"
       "*Verify all Identity objects in these volumes and report on their "
       "validity",0, 0, 'V'},

      {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, generate_short_optargs(long_options),
		    long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 0: {
      char *option = (char *)long_options[option_index].name;

      if(!strcmp(option, "chunks_per_segment")) {
	chunks_per_segment = atol(optarg);
	break;

      } if(!strcmp(option, "max_size")) {
	max_size = atol(optarg);
	break;

      } else {
	printf("Unknown long option %s", optarg);
	break;
      };
    };

    case 'c':
      cert = optarg;
      break;

    case 'k':
      key_file = optarg;
      break;

    case 'o':
      output_file = optarg;
      break;

    case 'd':
      driver = optarg;
      break;

    case 'e':
      extract = optarg;
      break;

    case 'm':
      mode = 'm';
      break;

    case 's':
      stream_name = optarg;
      break;

    case 'p':
      setenv("AFF4_PASSPHRASE", optarg, 1);
      break;

    case 'i':
      printf("Imaging Mode selected\n");
      mode = 'i';
      break;

    case 'I':
      printf("Info mode selected\n");
      mode = 'I';
      break;

    case 'v':
      //AFF4_DEBUG_LEVEL++;
      break;

    case 'V':
      verify = 1;
      break;

    case '?':
    case 'h':
      printf("%s - an AFF4 general purpose imager.\n", argv[0]);
      print_help(long_options);
      exit(0);

    default:
      printf("?? getopt returned character code 0%o ??\n", c);
    }
  }

    if(optind < argc) {
      // We are imaging now
      if(mode == 'i') {
        if(!output_file) {
          printf("You must specify an output file with --output\n");
          exit(-1);
        };

        while (optind < argc) {
          char *in_urn = argv[optind];
          char *in_stream_name = in_urn;

          if(stream_name) {
            in_stream_name = stream_name;
          };

          AFF4Stream *stream = AFF4FactoryOpen<AFF4Stream>(in_urn, "r");
          aff4_image(output_file,
                     in_stream_name,
                     chunks_per_segment,
                     max_size,
                     *stream);

          optind++;
        };
      } else if(mode == 'm') {
        /*
        aff4_make_map(driver, output_file, stream_name,
                      argv+optind, argc- optind);
        */
      };
      printf("\n");
    };

    return 0;
}
