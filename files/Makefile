# If we specified type=debug on the command line set the output directory root (BASEDIR) to Debug and create a macro
# (DEBUGFLAGS) to add debugging info (-g) and prevent all optimisation (-O0) as GDB stuggles with optimised code. If
# type=anything else or is not specified the output directory is Release
# 
ifeq ($(type), debug)
  BASEDIR = Debug
  DEBUGFLAGS = -g -O0  
else
  DEBUGFLAGS =
  BASEDIR = Release
endif

#======================================================================================================================
# The target architecture should be specified in the OECORE_TARGET_ARCH environment variable, which is set by the SDK.
# If for some reason it isn't set (we are not building with an SDK) we assume it's going to run on the build machine
# so set it to x86_64 which should be the local system architecture.
        
ifndef OECORE_TARGET_ARCH      
  OECORE_TARGET_ARCH = x86_64
endif   

CFLAGS = -c $(DEBUGFLAGS)
OBJDIR = $(BASEDIR)/$(OECORE_TARGET_ARCH)
      
all: directory $(OBJDIR)/ltc-monitor
  
clean: 
	mkdir -p $(OBJDIR)           
	rm -f $(OBJDIR)/*            

directory:
	mkdir -p $(OBJDIR)           
  
$(OBJDIR)/ltc-monitor : $(OBJDIR)/main.o
	$(CC) $(DEBUGFLAGS) -o $(OBJDIR)/ltc-monitor $(OBJDIR)/main.o

$(OBJDIR)/main.o : main.c
	$(CC) $(CFLAGS) -o $(OBJDIR)/main.o main.c
