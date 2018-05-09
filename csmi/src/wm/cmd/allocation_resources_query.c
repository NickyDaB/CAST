/*================================================================================

    csmi/src/wm/cmd/allocation_resources_query.c

  © Copyright IBM Corporation 2015-2017. All Rights Reserved

    This program is licensed under the terms of the Eclipse Public License
    v1.0 as published by the Eclipse Foundation and available at
    http://www.eclipse.org/legal/epl-v10.html

    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
    restricted by GSA ADP Schedule Contract with IBM Corp.

================================================================================*/
/*
* Author: John Dunham
* Email: jdunham@us.ibm.com
*/
/*C Include*/
#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
/*CORAL includes*/
#include "utilities/include/string_tools.h"
/*CSM Include*/
#include "csmi/include/csm_api_workload_manager.h"
/*Needed for infrastructure logging*/
#include "csmutil/include/csmutil_logging.h"
/* Command line macros for ease of use. */
#include "csmi/src/common/include/csmi_internal_macros.h"

/* Should we do this? */
#define API_PARAMETER_INPUT_TYPE csm_allocation_resources_query_input_t
#define API_PARAMETER_OUTPUT_TYPE csm_allocation_resources_query_output_t

///< For use as the usage variable in the input parsers.
#define USAGE csm_free_struct_ptr(API_PARAMETER_INPUT_TYPE, input); help

void help() {
	puts("_____CSM_ALLOCATION_NODE_RESOURCES_QUERY_ALL_CMD_HELP_____");
	puts("USAGE:");
	puts("  csm_allocation_resources_query ARGUMENTS [OPTIONS]");
	puts("  csm_allocation_resources_query [-a allocation_id] [-l limit] [-o offset] [-h] [-v verbose_level]");
	puts("");
	puts(" SUMMARY: Used to gather node details about nodes associated with the allocation id." );
	puts("");
	puts("EXIT STATUS:");
	puts("  0   if OK,");
	puts("  >0  if ERROR.");
	puts("");
	puts("ARGUMENTS:");
    puts("  MANDATORY:");
    puts("    csm_allocation_resources_query expects 1 mandatory arguments");
    puts("    Argument                | Example value | Description  ");               
    puts("    ------------------------|---------------|--------------");
    puts("    -a, --allocation_id     | 1             | (LONG INTEGER) Unique identifier for an allocation.");
	puts("  FILTERS:");
	puts("    csm_allocation_resources_query can have 2 optional filters.");
	puts("    Argument      | Example value | Description  ");                                                 
	puts("    --------------|---------------|--------------");
	puts("    -l, --limit   | 10            | (INTEGER) SQL 'LIMIT' numeric value.");
    puts("    -o, --offset  | 1             | (INTEGER) SQL 'OFFSET' numeric value.");
	puts("GENERAL OPTIONS:");
	puts("[-h, --help]                  | Help.");
	puts("[-v, --verbose verbose_level] | Set verbose level. Valid verbose levels: {off, trace, debug, info, warning, error, critical, always, disable}");
	puts("");
	puts("EXAMPLE OF USING THIS COMMAND:");
	puts("  csm_allocation_resources_query -a 1");
	puts("");
	puts("OUTPUT OF THIS COMMAND IS DISPLAYED IN THE YAML FORMAT.");
	puts("____________________");
}

static struct option long_options[] =
{
	//general options
	{"help",          no_argument,       0, 'h'},
	{"verbose",       required_argument, 0, 'v'},
	//api arguments
	//filters
	{"allocation_id",   required_argument, 0, 'a'},
	{"limit",           required_argument, 0, 'l'},
	{"offset",          required_argument, 0, 'o'},
	{0, 0, 0, 0}
};

/*
* Summary: Simple command line interface for the CSM API 'node resources query'. 
*			Works as interface to the CSM DB.
* 			Takes in the node names via command line parameters, and queries the data in the CSM database.
*/
int main(int argc, char *argv[])
{
	/*CSM Variables*/
	csm_api_object *csm_obj = NULL;
	/*Helper Variables*/
	int return_value = 0;
	int requiredParameterCounter = 0;
	int optionalParameterCounter = 0;
	const int NUMBER_OF_REQUIRED_ARGUMENTS = 1;
	const int MINIMUM_NUMBER_OF_OPTIONAL_ARGUMENTS = 0;
	/*Variables for checking cmd line args*/
	int opt;
	/* getopt_long stores the option index here. */
	int indexptr = 0;
	/*i var for 'for loops'*/
	uint32_t i = 0;
    char *arg_check = NULL; ///< Used in verifying the long arg values.
	
	/*Set up data to call API*/
	API_PARAMETER_INPUT_TYPE* input = NULL;
	/* CSM API initialize and malloc function*/
	csm_init_struct_ptr(API_PARAMETER_INPUT_TYPE, input);
	API_PARAMETER_OUTPUT_TYPE* output = NULL;
	
	/*check optional args*/
	while ((opt = getopt_long(argc, argv, "hv:a:l:o:", long_options, &indexptr)) != -1) {
    switch (opt) {
			case 'h':      
                USAGE();
				return CSMI_HELP;
			case 'v':
				/*Error check to make sure 'verbose' field is valid.*/
                csm_set_verbosity( optarg, USAGE )
				break;
            case 'a':
                csm_optarg_test( "-a, --allocation", optarg, USAGE )
                csm_str_to_int64(input->allocation_id, optarg, arg_check, "-a, --allocation", USAGE);
                requiredParameterCounter++;
                break;
			case 'l':
                csm_optarg_test( "-l, --limit", optarg, USAGE )
                csm_str_to_int32( input->limit, optarg, arg_check, "-l, --limit", USAGE );
				break;
			case 'o':
                csm_optarg_test( "-o, --offset", optarg, USAGE )
                csm_str_to_int32( input->offset, optarg, arg_check, "-o, --offset", USAGE );
				break;
			default:
				csmutil_logging(error, "unknown arg: '%c'\n", opt);
                USAGE();
				return CSMERR_INVALID_PARAM;
		}
	}
	
	/*Handle command line args*/
	argc -= optind;
	argv += optind;
	
	/*Collect mandatory args*/
	/*Check to see if expected number of arguments is correct.*/
	if(requiredParameterCounter < NUMBER_OF_REQUIRED_ARGUMENTS || optionalParameterCounter < MINIMUM_NUMBER_OF_OPTIONAL_ARGUMENTS){
		/*We don't have the correct number of needed arguments passed in.*/
		csmutil_logging(error, "%s-%d:", __FILE__, __LINE__);
		csmutil_logging(error, "  Missing operand(s).");
		csmutil_logging(error, "    Encountered %i required parameter(s). Expected %i required parameter(s).", requiredParameterCounter, NUMBER_OF_REQUIRED_ARGUMENTS);
		csmutil_logging(error, "    Encountered %i optional parameter(s). Expected at least %i optional parameter(s).", optionalParameterCounter, MINIMUM_NUMBER_OF_OPTIONAL_ARGUMENTS);
        USAGE();
		return CSMERR_MISSING_PARAM;

	}
	
	/* Success required to be able to communicate between library and daemon - csmi calls must be made inside the frame created by csm_init_lib() and csm_term_lib()*/
	return_value = csm_init_lib();
	if( return_value != 0)
    {
		csmutil_logging(error, "%s-%d:", __FILE__, __LINE__);
		csmutil_logging(error, "  csm_init_lib rc= %d, Initialization failed. Success is required "
            "to be able to communicate between library and daemon. Are the daemons running?", 
            return_value);
		csm_free_struct_ptr(API_PARAMETER_INPUT_TYPE, input);
		return return_value;            
	}
	
	/* Call the C API. */
	return_value = csm_allocation_resources_query(&csm_obj, input, &output);
	/* Use CSM API free to release arguments. We no longer need them. */
	csm_free_struct_ptr(API_PARAMETER_INPUT_TYPE, input);

    switch(return_value)
    {
        case CSMI_SUCCESS:
			puts("---");
			printf("Total_Records: %i\n", output->results_count);
			for(i = 0; i < output->results_count; i++){
				printf("Record_%i:\n", i+1);
				printf("  node_name:  %s\n", output->results[i]->node_name);
				printf("  ready:      %c\n", csm_print_bool_custom(output->results[i]->ready, 'y','n'));
			}
			puts("...");
            break;

        case CSMI_NO_RESULTS:
            puts("---");
            printf("Total_Records: 0\n");
            puts("# No matching records found.");
            puts("...");
            break;

        default:
            printf("%s FAILED: errcode: %d errmsg: %s\n",
                argv[0], return_value,  csm_api_object_errmsg_get(csm_obj));
    }
	
	/* Call internal CSM API clean up. */
    csm_api_object_destroy(csm_obj);
	
    // Cleanup the library and print the error.
	int lib_return_value = csm_term_lib();
	if( lib_return_value != 0 )
    {
		csmutil_logging(error, "csm_term_lib rc= %d, Initialization failed. Success "
            "is required to be able to communicate between library and daemon. Are the "
            "daemons running?", lib_return_value);
		return lib_return_value;
	}

	return return_value;
}
