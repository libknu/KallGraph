# Configurations

KERNEL_SRC="/path/to/linux-6.5-src"
IRDUMPER="$(pwd)/IRDumper/build/lib/libDumper.so"
CLANG="/path/to/your/llvm-14.0.6.build/bin/clang"
CONFIG="defconfig"
# CONFIG="allyesconfig"

# Use -Wno-error to avoid turning warnings into errors
# O0
NEW_CMD="\n\nKBUILD_USERCFLAGS += -Wno-error -O0 -fno-discard-value-names -finline-functions -g -Xclang -disable-O0-optnone -Xclang -finline-functions -Xclang -flegacy-pass-manager -Xclang -load -Xclang $IRDUMPER\n
KBUILD_CFLAGS += -Wno-error -O0 -fno-discard-value-names -finline-functions -g -Xclang -finline-functions -Xclang -disable-O0-optnone -Xclang -flegacy-pass-manager -Xclang -load -Xclang $IRDUMPER"

# Back up Linux Makefile
if [ ! -f "$KERNEL_SRC/Makefile.bak" ]; then
	cp $KERNEL_SRC/Makefile $KERNEL_SRC/Makefile.bak
fi

# The new flags better follow "# Add user supplied CPPFLAGS, AFLAGS and CFLAGS as the last assignments"
echo -e $NEW_CMD >$KERNEL_SRC/IRDumper.cmd
cat $KERNEL_SRC/Makefile.bak $KERNEL_SRC/IRDumper.cmd >$KERNEL_SRC/Makefile

cd $KERNEL_SRC && make $CONFIG
echo $CLANG
echo $NEW_CMD
make CC=$CLANG -j72 -k -i V=1 2>&1 | tee make.log

