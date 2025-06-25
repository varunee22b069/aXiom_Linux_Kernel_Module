obj-m += axiom_i2c_drv.o

axiom_i2c_drv-objs := axiom_core.o axiom_i2c.o axiom_ioctl.o axiom_cfg.o axiom_fw.o

export KERNELDIR=/lib/modules/$(shell uname -r)/build/

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
