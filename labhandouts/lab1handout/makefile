PIN_ROOT = ../../base/pin/
TOOL_ROOTS = regDeps

all:
	g++ -Wall -Werror -Wno-unknown-pragmas -D__PIN__=1 -DPIN_CRT=1 -fno-stack-protector -fno-exceptions -funwind-tables -fasynchronous-unwind-tables -fno-rtti -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX -fabi-version=2  -I$(PIN_ROOT)/source/include/pin -I$(PIN_ROOT)/source/include/pin/gen -isystem $(PIN_ROOT)extras/stlport/include -isystem $(PIN_ROOT)extras/libstdc++/include -isystem $(PIN_ROOT)extras/crt/include -isystem $(PIN_ROOT)extras/crt/include/arch-x86_64 -isystem $(PIN_ROOT)extras/crt/include/kernel/uapi -isystem $(PIN_ROOT)extras/crt/include/kernel/uapi/asm-x86 -I$(PIN_ROOT)/extras/components/include -I$(PIN_ROOT)/extras/xed-intel64/include/xed -I$(PIN_ROOT)/source/tools/InstLib -O3 -fomit-frame-pointer -fno-strict-aliasing   -c -o $(TOOL_ROOTS).o $(TOOL_ROOTS).cpp
	g++ -shared -Wl,--hash-style=sysv $(PIN_ROOT)/intel64/runtime/pincrt/crtbeginS.o -Wl,-Bsymbolic -Wl,--version-script=$(PIN_ROOT)/source/include/pin/pintool.ver -fabi-version=2    -o $(TOOL_ROOTS).so $(TOOL_ROOTS).o  -L$(PIN_ROOT)/intel64/runtime/pincrt -L$(PIN_ROOT)/intel64/lib -L$(PIN_ROOT)/intel64/lib-ext -L$(PIN_ROOT)/extras/xed-intel64/lib -lpin -lxed $(PIN_ROOT)/intel64/runtime/pincrt/crtendS.o -lpin3dwarf  -ldl-dynamic -nostdlib -lstlport-dynamic -lm-dynamic -lc-dynamic -lunwind-dynamic

clean:
	-rm -f *.o *.so *.out *.tested *.failed *.d *.makefile.copy *.exp *.lib *.log

realclean:
	-rm -rf *.o *so *.out *.tested *.failed *.d *.makefile.copy *.exp *.lib *results_* *.out *.log outputs_* _temp*
