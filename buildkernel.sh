# Copyright © 2016, edited by krizky82
#
# This software is licensed under the terms of the GNU General Public
# License version 2, as published by the Free Software Foundation, and
# may be copied, distributed, and modified under those terms.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# Please maintain this if you use this script or any part of it

# Init Script
KERNEL_DIR=$PWD
KERNEL="Image.gz-dtb"
KERN_IMG=$KERNEL_DIR/out/arch/arm64/boot/Image.gz-dtb
MODULE=$KERNEL_DIR/out/

BUILD_START=$(date +"%s")
#ANYKERNEL_DIR=/root/AnyKernel2
ANYKERNEL_DIR=/home/krizky/Kernel/AnyKernel2
#EXPORT_DIR=/root/flashablezips
EXPORT_DIR=/home/krizky/Kernel/flashablezips

# Make Changes to this before release
ZIP_NAME="krizky-1"

# Tweakable Options Below
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER="krizky82"
export KBUILD_BUILD_HOST="Ubuntu-WSL"
export CROSS_COMPILE="/home/krizky/Kernel/Linaro/bin/aarch64-linux-gnu-"
export KBUILD_COMPILER_STRING=$(/home/krizky/Kernel/Clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')

echo "  Initializing build to compile Ver: $ZIP_NAME    "

echo "         Creating Output Directory: out      "

mkdir -p out

echo "          Cleaning Up Before Compile          "

make O=out mrproper

echo "          Initialising DEFCONFIG        "

make O=out ARCH=arm64 whyred-perf_defconfig

echo "          Cooking Kernel....        "

make -j$(nproc --all) O=out \
                      ARCH=arm64 \
		      CC="/home/krizky/Kernel/Clang/bin/clang" \
                      CLANG_TRIPLE=aarch64-linux-gnu- \
                      CROSS_COMPILE="/home/krizky/Kernel/Linaro/bin/aarch64-linux-gnu-"

# If the above was successful
if [ -a $KERN_IMG ]; then
   BUILD_RESULT_STRING="BUILD SUCCESSFUL"

echo "       Making Flashable Zip       "

   # Make the zip file
   echo "MAKING FLASHABLE ZIP"

#Copy zImage to AnyKernel2

 rm -f ${ANYKERNEL_DIR}/Image.gz*                 
 rm -f ${ANYKERNEL_DIR}/zImage*                    
 rm -f ${ANYKERNEL_DIR}/dtb*                  

cp -vr ${KERN_IMG} ${ANYKERNEL_DIR}/zImage  
 
#Modules are now build inside the kernel, no need to export new modules to AnyKernel2
#
#rm -rf ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules

#mkdir -p ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules

#cp ${MODULE}block/test-iosched.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/test-iosched.ko
#cp ${MODULE}drivers/char/rdbg.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/rdbg.ko
#cp ${MODULE}drivers/media/platform/msm/dvb/adapter/mpq-adapter.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/mpq-adapter.ko
#cp ${MODULE}drivers/media/platform/msm/dvb/demux/mpq-dmx-hw-plugin.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/mpq-dmx-hw-plugin.ko
#cp ${MODULE}drivers/media/usb/gspca/gspca_main.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/gspca_main.ko
#cp ${MODULE}drivers/net/wireless/ath/wil6210/wil6210.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/wil6210.ko
#cp ${MODULE}drivers/platform/msm/msm_11ad/msm_11ad_proxy.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/msm_11ad_proxy.ko
#cp ${MODULE}drivers/scsi/ufs/ufs_test.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/ufs_test.ko
#cp ${MODULE}fs/exfat/exfat.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/exfat.ko
#cp ${MODULE}net/bridge/br_netfilter.ko ${ANYKERNEL_DIR}/modules/system/vendor/lib/modules/br_netfilter.ko
   cd ${ANYKERNEL_DIR}

   rm *.zip 

   zip -r9 ${ZIP_NAME}.zip * -x README ${ZIP_NAME}.zip

else
   BUILD_RESULT_STRING="BUILD FAILED"
fi

NOW=$(date +"%m-%d-%H-%M")
ZIP_LOCATION=${ANYKERNEL_DIR}/${ZIP_NAME}.zip
ZIP_EXPORT=${EXPORT_DIR}/${NOW}
ZIP_EXPORT_LOCATION=${EXPORT_DIR}/${NOW}/${ZIP_NAME}.zip

rm -rf ${ZIP_EXPORT}
mkdir ${ZIP_EXPORT}
cp ${ZIP_LOCATION} ${ZIP_EXPORT}
cd ${HOME}

# End the script
echo "${BUILD_RESULT_STRING}!"

# BUILD TIME
BUILD_END=$(date +"%s")
DIFF=$(($BUILD_END - $BUILD_START))
echo -e "$Yellow Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds.$nocol"
