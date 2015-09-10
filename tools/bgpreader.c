/*
 * This file is part of bgpstream
 *
 * CAIDA, UC San Diego
 * bgpstream-info@caida.org
 *
 * Copyright (C) 2012 The Regents of the University of California.
 * Authors: Alistair King, Chiara Orsini
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>

#include "bgpstream.h"

#define PROJECT_CMD_CNT 10
#define TYPE_CMD_CNT    10
#define COLLECTOR_CMD_CNT 100
#define WINDOW_CMD_CNT 1024
#define OPTION_CMD_CNT 1024

struct window {
  uint32_t start;
  uint32_t end;
};

static bgpstream_t *bs;
static bgpstream_data_interface_id_t datasource_id_default = 0;
static bgpstream_data_interface_id_t datasource_id = 0;
static bgpstream_data_interface_info_t *datasource_info = NULL;

static void data_if_usage() {
  bgpstream_data_interface_id_t *ids = NULL;
  int id_cnt = 0;
  int i;

  bgpstream_data_interface_info_t *info = NULL;

  id_cnt = bgpstream_get_data_interfaces(bs, &ids);

  for(i=0; i<id_cnt; i++)
    {
      info = bgpstream_get_data_interface_info(bs, ids[i]);

      if(info != NULL)
        {
          fprintf(stderr,
                  "       %-15s%s%s\n", info->name, info->description,
                  (ids[i] == datasource_id_default) ? " (default)" : "");
        }
    }
}

static void dump_if_options() {
  assert(datasource_id != 0);

  bgpstream_data_interface_option_t *options;
  int opt_cnt = 0;
  int i;

  opt_cnt = bgpstream_get_data_interface_options(bs, datasource_id, &options);

  fprintf(stderr, "Data interface options for '%s':\n", datasource_info->name);
  if(opt_cnt == 0)
    {
      fprintf(stderr, "   [NONE]\n");
    }
  else
    {
      for(i=0; i<opt_cnt; i++)
        {
          fprintf(stderr, "   %-15s%s\n",
                  options[i].name, options[i].description);
        }
    }
  fprintf(stderr, "\n");
}

static void usage() {
  fprintf(stderr,
	  "usage: bgpreader -w <start,end> [<options>]\n"
          "Available options are:\n"
          "   -d <interface> use the given data interface to find available data\n"
          "                  available data interfaces are:\n");
  data_if_usage();
  fprintf(stderr,
          "   -o <option-name,option-value>*\n"
          "                  set an option for the current data interface.\n"
          "                  use '-o ?' to get a list of available options for the current\n"
          "                  data interface. (data interface can be selected using -d)\n"
	  "   -p <project>   process records from only the given project (routeviews, ris)*\n"
	  "   -c <collector> process records from only the given collector*\n"
	  "   -t <type>      process records with only the given type (ribs, updates)*\n"
	  "   -w <start,end> process records only within the given time window*\n"
          "   -P <period>    process a rib files every <period> seconds (bgp time)\n"
	  "   -b             make blocking requests for BGP records\n"
	  "                  allows bgpstream to be used to process data in real-time\n"
          "\n"
	  "   -r             print info for each BGP record (default)\n"
          "   -m             print info for each BGP valid record in bgpdump -m format\n"
          "   -e             print info for each element of a valid BGP record\n"         
          "\n"
	  "   -h             print this help menu\n"
	  "* denotes an option that can be given multiple times\n"
	  );
}

// print functions

static void print_bs_record(bgpstream_record_t * bs_record);
static int print_elem(bgpstream_elem_t *elem);


int main(int argc, char *argv[])
{

  int opt;
  int prevoptind;

  opterr = 0;

  // variables associated with options
  char *projects[PROJECT_CMD_CNT];
  int projects_cnt = 0;

  char *types[TYPE_CMD_CNT];
  int types_cnt = 0;

  char *collectors[COLLECTOR_CMD_CNT];
  int collectors_cnt = 0;

  struct window windows[WINDOW_CMD_CNT];
  char *endp;
  int windows_cnt = 0;

  char *interface_options[OPTION_CMD_CNT];
  int interface_options_cnt = 0;

  int rib_period = 0;
  int blocking = 0;
  int record_output_on = 0;
  int record_bgpdump_output_on = 0;
  int elem_output_on = 0;

  bgpstream_data_interface_option_t *option;

  int i;


  // required to be created before usage is called
  bs = bgpstream_create();
  if(!bs) {
    fprintf(stderr, "ERROR: Could not create BGPStream instance\n");
    return -1;
  }
  datasource_id_default = datasource_id = bgpstream_get_data_interface_id(bs);
  datasource_info = bgpstream_get_data_interface_info(bs, datasource_id);
  assert(datasource_id != 0);

  while (prevoptind = optind,
	 (opt = getopt (argc, argv, "d:o:p:c:t:w:P:brmeh?")) >= 0)
    {
      if (optind == prevoptind + 2 && (optarg == NULL || *optarg == '-') ) {
        opt = ':';
        -- optind;
      }
      switch (opt)
	{
	case 'p':
	  if(projects_cnt == PROJECT_CMD_CNT)
	    {
	      fprintf(stderr,
		      "ERROR: A maximum of %d projects can be specified on "
		      "the command line\n",
		      PROJECT_CMD_CNT);
	      usage();
	      exit(-1);
	    }
	  projects[projects_cnt++] = strdup(optarg);
	  break;
	case 'c':
	  if(collectors_cnt == COLLECTOR_CMD_CNT)
	    {
	      fprintf(stderr,
		      "ERROR: A maximum of %d collectors can be specified on "
		      "the command line\n",
		      COLLECTOR_CMD_CNT);
	      usage();
	      exit(-1);
	    }
	  collectors[collectors_cnt++] = strdup(optarg);
	  break;
	case 't':
	  if(types_cnt == TYPE_CMD_CNT)
	    {
	      fprintf(stderr,
		      "ERROR: A maximum of %d types can be specified on "
		      "the command line\n",
		      TYPE_CMD_CNT);
	      usage();
	      exit(-1);
	    }
	  types[types_cnt++] = strdup(optarg);
	  break;
	case 'w':
	  if(windows_cnt == WINDOW_CMD_CNT)
	  {
	    fprintf(stderr,
		    "ERROR: A maximum of %d windows can be specified on "
		    "the command line\n",
		    WINDOW_CMD_CNT);
	    usage();
	    exit(-1);
	  }
	  /* split the window into a start and end */
	  if((endp = strchr(optarg, ',')) == NULL)
	    {
	      fprintf(stderr, "ERROR: Malformed time window (%s)\n", optarg);
	      fprintf(stderr, "ERROR: Expecting start,end\n");
	      usage();
	      exit(-1);
	    }
	  *endp = '\0';
	  endp++;
	  windows[windows_cnt].start = atoi(optarg);
	  windows[windows_cnt].end =  atoi(endp);
	  windows_cnt++;
	  break;
        case 'P':
          rib_period = atoi(optarg);
          break;
	case 'd':
          if((datasource_id =
              bgpstream_get_data_interface_id_by_name(bs, optarg)) == 0)
            {
              fprintf(stderr, "ERROR: Invalid data interface name '%s'\n",
                      optarg);
              usage();
              exit(-1);
            }
          datasource_info =
            bgpstream_get_data_interface_info(bs, datasource_id);
	  break;
        case 'o':
          if(interface_options_cnt == OPTION_CMD_CNT)
	    {
	      fprintf(stderr,
		      "ERROR: A maximum of %d interface options can be specified\n",
		      OPTION_CMD_CNT);
	      usage();
	      exit(-1);
	    }
	  interface_options[interface_options_cnt++] = strdup(optarg);
          break;

	case 'b':
	  blocking = 1;
	  break;
	case 'r':
	  record_output_on = 1;
	  break;
	case 'm':
	  record_bgpdump_output_on = 1;
	  break;
	case 'e':
	  elem_output_on = 1;
	  break;
	case ':':
	  fprintf(stderr, "ERROR: Missing option argument for -%c\n", optopt);
	  usage();
	  exit(-1);
	  break;
	case '?':
	case 'v':
	  fprintf(stderr, "bgpreader version %d.%d.%d\n",
		  BGPSTREAM_MAJOR_VERSION,
		  BGPSTREAM_MID_VERSION,
		  BGPSTREAM_MINOR_VERSION);
	  usage();
	  exit(0);
	  break;
	default:
	  usage();
	  exit(-1);
	}
    }

  for(i=0; i<interface_options_cnt; i++)
    {
      if(*interface_options[i] == '?')
        {
          dump_if_options();
          usage();
          exit(0);
        }
      else
        {
          /* actually set this option */
          if((endp = strchr(interface_options[i], ',')) == NULL)
            {
              fprintf(stderr,
                      "ERROR: Malformed data interface option (%s)\n",
                      interface_options[i]);
              fprintf(stderr,
                      "ERROR: Expecting <option-name>,<option-value>\n");
              usage();
              exit(-1);
            }
          *endp = '\0';
          endp++;
          if((option =
              bgpstream_get_data_interface_option_by_name(bs, datasource_id,
                                                          interface_options[i])) == NULL)
            {
              fprintf(stderr,
                      "ERROR: Invalid option '%s' for data interface '%s'\n",
                      interface_options[i], datasource_info->name);
              usage();
              exit(-1);
            }
          bgpstream_set_data_interface_option(bs, option, endp);
        }
      free(interface_options[i]);
      interface_options[i] = NULL;
    }
  interface_options_cnt = 0;

  if(windows_cnt == 0)
    {
      fprintf(stderr,
              "ERROR: At least one time window must be specified using -w\n");
      usage();
      exit(-1);
    }

  // if the user did not specify any output format
  // then the default one is per record
  if(record_output_on == 0 && elem_output_on == 0 && record_bgpdump_output_on == 0) {
    record_output_on = 1;
  }

  // the program can now start

  // allocate memory for interface

  /* projects */
  for(i=0; i<projects_cnt; i++)
    {
      bgpstream_add_filter(bs, BGPSTREAM_FILTER_TYPE_PROJECT, projects[i]);
      free(projects[i]);
    }

  /* collectors */
  for(i=0; i<collectors_cnt; i++)
    {
      bgpstream_add_filter(bs, BGPSTREAM_FILTER_TYPE_COLLECTOR, collectors[i]);
      free(collectors[i]);
    }

  /* types */
  for(i=0; i<types_cnt; i++)
    {
      bgpstream_add_filter(bs, BGPSTREAM_FILTER_TYPE_RECORD_TYPE, types[i]);
      free(types[i]);
    }

  /* windows */
  for(i=0; i<windows_cnt; i++)
    {
      bgpstream_add_interval_filter(bs, windows[i].start, windows[i].end);
    }

  /* frequencies */
  if(rib_period > 0)
    {
      bgpstream_add_rib_period_filter(bs, rib_period);
    }
    
  /* datasource */
  bgpstream_set_data_interface(bs, datasource_id);

  /* blocking */
  if(blocking != 0)
    {
      bgpstream_set_blocking(bs);
    }

   // allocate memory for bs_record
  bgpstream_record_t *bs_record = bgpstream_record_create();
  if(bs_record == NULL)
    {
      fprintf(stderr, "ERROR: Could not create BGPStream record\n");
      bgpstream_destroy(bs);
      return -1;
    }

    // turn on interface
  if(bgpstream_start(bs) < 0) {
    fprintf(stderr, "ERROR: Could not init BGPStream\n");
    return -1;
  }

  // use the interface
  int get_next_ret = 0;
  bgpstream_elem_t * bs_elem;
  do
    {
      get_next_ret = bgpstream_get_next_record(bs, bs_record);
      if(get_next_ret && record_output_on)
	{
	  print_bs_record(bs_record);
	}
      if(get_next_ret && bs_record->status == BGPSTREAM_RECORD_STATUS_VALID_RECORD)
	{
	  if(record_bgpdump_output_on)
	    {
	      bgpstream_record_print_mrt_data(bs_record);
	    }
	  if(elem_output_on)
	    {
	      while((bs_elem =
                     bgpstream_record_get_next_elem(bs_record)) != NULL)
		{
		  if(print_elem(bs_elem) != 0)
                    {
                      goto err;
                    }
		}
	    }
	}
  }
  while(get_next_ret > 0);

  // de-allocate memory for bs_record
  bgpstream_record_destroy(bs_record);

  // turn off interface
  bgpstream_stop(bs);

  // deallocate memory for interface
  bgpstream_destroy(bs);

  return 0;

 err:
  bgpstream_record_destroy(bs_record);
  bgpstream_stop(bs);
  bgpstream_destroy(bs);
  return -1;
}

// print utility functions

static char* get_dump_type_str(bgpstream_record_dump_type_t dump_type)
{
  switch(dump_type)
    {
    case BGPSTREAM_UPDATE:
      return "update";
    case BGPSTREAM_RIB:
      return "rib";
    }
  return "";
}

static char* get_dump_pos_str(bgpstream_dump_position_t dump_pos)
{
  switch(dump_pos)
    {
    case BGPSTREAM_DUMP_START:
      return "start";
    case BGPSTREAM_DUMP_MIDDLE:
      return "middle";
    case BGPSTREAM_DUMP_END:
      return "end";
    }
  return "";
}

static char *get_record_status_str(bgpstream_record_status_t status)
{
  switch(status)
    {
    case BGPSTREAM_RECORD_STATUS_VALID_RECORD:
      return "valid_record";
    case BGPSTREAM_RECORD_STATUS_FILTERED_SOURCE:
      return "filtered_source";
    case BGPSTREAM_RECORD_STATUS_EMPTY_SOURCE:
      return "empty_source";
    case BGPSTREAM_RECORD_STATUS_CORRUPTED_SOURCE:
      return "corrupted_source";
    case BGPSTREAM_RECORD_STATUS_CORRUPTED_RECORD:
      return "corrupted_record";
    }
  return "";
}


static void print_bs_record(bgpstream_record_t * bs_record)
{
  assert(bs_record);
  printf("%ld|", bs_record->attributes.record_time);
  printf("%s|", bs_record->attributes.dump_project);
  printf("%s|", bs_record->attributes.dump_collector);
  printf("%s|", get_dump_type_str(bs_record->attributes.dump_type));
  printf("%s|", get_record_status_str(bs_record->status));
  printf("%ld|", bs_record->attributes.dump_time);
  printf("%s|", get_dump_pos_str(bs_record->dump_pos));
  printf("\n");

}

static char elem_buf[65536];
static int print_elem(bgpstream_elem_t *elem)
{
  assert(elem);

  if(bgpstream_elem_snprintf(elem_buf, 65536, elem) == NULL)
    {
      fprintf(stderr, "Failed to construct elem string\n");
      elem_buf[65535] = '\0';
      fprintf(stderr, "Elem string: %s\n", elem_buf);
      return -1;
    }
  printf("%s\n", elem_buf);
  return 0;
}