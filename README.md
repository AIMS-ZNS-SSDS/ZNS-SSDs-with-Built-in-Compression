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

