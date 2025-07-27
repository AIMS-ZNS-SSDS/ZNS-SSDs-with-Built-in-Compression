# README

## balloon-zns

femu: https://github.com/MoatLab/FEMU

核心代码在`hw/femu/zns`文件夹。需要自己按官方github文档编译然后运行`run_zns.sh`。

zns文件夹下的mode-selection.h中，COMPQAT和NO_FUNC是用于开启压缩功能的宏，FEMU_DEBUG_NVME用于输出一些中间结果。

关于**qat**，我们代码在主文件夹下的meson.build有如下内容：

```
# added by zwl qat
  qat_deps = [cc.find_library('qat_s', dirs: '/home/weipanyue/femu-original/hw/femu/zns/qat/')]
  qat_deps += cc.find_library('usdm_drv_s', dirs: '/home/weipanyue/femu-original/hw/femu/zns/qat/')
```

会依赖服务器中wpy师姐的代码。 在实验室服务器外要使用时这里必须改动。 详细不在此记录, 请询问wpy师姐.

## IncSlotZNS

是目前此代码的最终版本，提出了一种新的分析窗口设计方法，已作为本科毕设内容。目前暂时不再继续研究（2025/7/27）。
大体思路可参考我主页的说明：<https://bystreamzhang.github.io/2025/02/28/balloon-zns-you-hua-ji-lu/>


## fio

fio官方github：[axboe/fio: Flexible I/O Tester](https://github.com/axboe/fio)

从源码编译fio：
```
./configure
make
make install
```
我对fio源码进行了修改，可以使用参数`input_file`指定输入文件。

我的修改如下：

首先在`thread_options`结构体加入`char *input_file;`；
然后在`option.c::fio_options`数组加入以下内容：

```c
{
		.name     = "input_file",
		.lname    = "Input filename",
		.type     = FIO_OPT_STR_STORE,
		.off1     = offsetof(struct thread_options, input_file),
		.help     = "File you want fio to use for writing test",
		.maxlen   = PATH_MAX,
		.category = FIO_OPT_C_FILE,
		.group	  = FIO_OPT_G_FILENAME,
	},
```

在`cconv.c::free_thread_options_to_cpu`我还加入了`free(o->input_file);`回收这部分空间。

然后在`io_u.c::fill_io_buffer`中，创建o的后面加入以下代码:
```c
// Check if input_file is set and valid
if (o->input_file && strlen(o->input_file) > 0) {
    FILE *file = fopen(o->input_file, "rb"); // Open the file for reading
    if (!file) {
        perror("Failed to open input file"); // Error handling
        return; // Exit if file cannot be opened
    }

    // Read data from the file into the buffer
    size_t bytesRead = fread(buf, 1, max_bs, file);
    if (bytesRead < max_bs) {
        if (ferror(file)) {
            perror("Error reading from input file"); // Read error handling
        }
    }
    log_info("[znbc] Successfully read the input file.\n");
    fclose(file); // Close the file after reading
    return; // Exit after reading the buffer
}
```
fio测试文件或指令中，加入`input_file=XXX`，其中右边改成要输入的文件的路径，即可进行测试。

我的fio顺序写测试配置文件：
```
[global]
ioengine=psync
direct=1
filename=/dev/nvme0n1
bs=256k
group_reporting
zonemode=zbd
offset_increment=4z
size=4z
input_file=/home/znbc/fio-test/osdb

[seq_write]
rw=write
```

我的测试数据和文件都比较简单，复杂的情况不确定能否正常测试。

## YCSB

我使用YCSB-cpp进行测试，对代码进行了一定修改

仓库：https://github.com/bystreamzhang/YCSB-cpp-modification

## run-zns.sh脚本

补充一下，这个脚本由于是编译产物并且经常修改，没有上传到github，最终版本如下，可自行替换修改：

```sh
#!/bin/bash
#
# Huaicheng Li <huaicheng@vt.edu>
# Run FEMU as Zoned-Namespace (ZNS) SSDs
#

# 本脚本未上传到github

# Image directory
IMGDIR=$HOME/images
# Virtual machine disk image
OSIMGF=$IMGDIR/femu_zwl.qcow2

# 自定义内核文件夹, 需包含bzImage和initrd. 加-k启用该功能
# 修改KERDIR的值来改变自定义的内核

#KERDIR=$IMGDIR/kernel_CCZNS
KERDIR=$IMGDIR/kernel_FlexZNS
#ZNSNAME=cczns
ZNSNAME=flexzns

ROOT=/dev/mapper/ubuntu--vg-ubuntu--lv

: '
KERNEL_OPTIONS=" \
    -kernel $KERDIR/bzImage \
    -initrd $KERDIR/initrd.img
"
'

rm output.txt
exec > >(tee -a output.txt) 2>&1

KERNEL_OPTIONS=()

# 解析脚本参数
for arg in "$@"; do
    case $arg in
        -k)
            KERNEL_OPTIONS+=(
                -kernel $KERDIR/bzImage-$ZNSNAME
                -initrd $KERDIR/initrd-$ZNSNAME.img
            )
            echo "Use custom kernel."
            echo "Kernel Options: ${KERNEL_OPTIONS[@]}"
            ;;
    esac
done

# 检查内核文件
if [[ "${#KERNEL_OPTIONS[@]}" != 0 ]]; then
    if [[ ! -e "$KERDIR/bzImage-$ZNSNAME" ]]; then
        echo ""
        echo "Kernel bzImage couldn't be found ..."
        echo "Please prepare a usable bzImage and place it as $KERDIR/bzImage"
        echo "Or disable this feature in script"
        echo ""
        exit
    fi

    if [[ ! -e "$KERDIR/initrd-$ZNSNAME.img" ]]; then
        echo ""
        echo "Kernel initrd couldn't be found ..."
        echo "Please prepare a usable initrd and place it as $KERDIR/initrd.img"
        echo "Or disable this feature in script"
        echo ""
        exit
    fi
    echo "KERNEL_DIR: $KERDIR"
else
    echo "Use default kernel."
fi

# 新增共享文件夹配置
SHARED_FOLDER_OPTIONS=" \
    -fsdev local,security_model=passthrough,id=fsdev0,path=/home/zhangnabaichuan/host_share \
    -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=hostshare
"

if [[ ! -e "$OSIMGF" ]]; then
	echo ""
	echo "VM disk image couldn't be found ..."
	echo "Please prepare a usable VM image and place it as $OSIMGF"
	echo "Once VM disk image is ready, please rerun this script again"
	echo ""
	exit
fi

#SSD_SIZE_MB=4096
SSD_SIZE_MB=8192 # old one is 4096 , 8192 is for rocksdb test (need 32 zones), also need to enable NUM_PAGE_FIXED in mode-selection.h. 
NUM_CHANNELS=2 # keep pace with CHBITS in struct ppa
NUM_CHIPS_PER_CHANNEL=4 
#NUM_CHIPS_PER_CHANNEL=8 # same as the Balloon-ZNS paper
NUM_PLANES_PER_CHIP=2
#NUM_BLOCKS_PER_CHIP=32
NUM_BLOCKS_PER_PLANE=32
# SLC:1 MLC:2 TLC:3 QLC:4
# MLC is not allowed
FLASH_TYPE=4

FEMU_OPTIONS="-device femu"
FEMU_OPTIONS=${FEMU_OPTIONS}",devsz_mb=${SSD_SIZE_MB}"
FEMU_OPTIONS=${FEMU_OPTIONS}",namespaces=1"
FEMU_OPTIONS=${FEMU_OPTIONS}",zns_num_ch=${NUM_CHANNELS}"
FEMU_OPTIONS=${FEMU_OPTIONS}",zns_num_lun=${NUM_CHIPS_PER_CHANNEL}"
FEMU_OPTIONS=${FEMU_OPTIONS}",zns_num_plane=${NUM_PLANES_PER_CHIP}"
FEMU_OPTIONS=${FEMU_OPTIONS}",zns_num_blk=${NUM_BLOCKS_PER_PLANE}"
FEMU_OPTIONS=${FEMU_OPTIONS}",zns_flash_type=${FLASH_TYPE}"
FEMU_OPTIONS=${FEMU_OPTIONS}",femu_mode=3"



# If using gdb, before input `run`, input `handle SIGUSR1 nostop` 
# sudo gdb --args ./qemu-system-x86_64 \

sleep 1s

if [[ "${#KERNEL_OPTIONS[@]}" != 0 ]]; then

sudo ./qemu-system-x86_64 \
    -name "FEMU-ZNSSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -m 12G \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    "${KERNEL_OPTIONS[@]}" \
    -append "root=$ROOT console=ttyS0 nokaslr" \
    -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
    ${FEMU_OPTIONS} \
    ${SHARED_FOLDER_OPTIONS} \
    -net user,hostfwd=tcp::9090-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log

else

sudo ./qemu-system-x86_64 \
    -name "FEMU-ZNSSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 4 \
    -m 4G \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    "${KERNEL_OPTIONS[@]}" \
    -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
    ${FEMU_OPTIONS} \
    ${SHARED_FOLDER_OPTIONS} \
    -net user,hostfwd=tcp::9090-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log

fi
```