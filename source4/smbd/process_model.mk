# Server process model subsystem

################################################
# Start MODULE process_model_single
[MODULE::process_model_single]
INIT_FUNCTION = process_model_single_init 
SUBSYSTEM = PROCESS_MODEL
OBJ_FILES = \
		process_single.o
# End MODULE process_model_single
################################################

################################################
# Start MODULE process_model_standard
[MODULE::process_model_standard]
INIT_FUNCTION = process_model_standard_init 
SUBSYSTEM = PROCESS_MODEL
OBJ_FILES = \
		process_standard.o
# End MODULE process_model_standard
################################################

################################################
# Start MODULE process_model_thread
[MODULE::process_model_thread]
INIT_FUNCTION = process_model_thread_init 
SUBSYSTEM = PROCESS_MODEL
OBJ_FILES = \
		process_thread.o
REQUIRED_SUBSYSTEMS = EXT_LIB_PTHREAD
# End MODULE process_model_thread
################################################

################################################
# Start SUBSYSTEM PROCESS_MODEL
[SUBSYSTEM::PROCESS_MODEL]
OBJ_FILES = \
		process_model.o
#
# End SUBSYSTEM PROCESS_MODEL
################################################
