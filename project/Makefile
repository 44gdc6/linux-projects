.PHONY: all app drivers clean

all: app drivers

app:
	$(MAKE) -C app

drivers:
	$(MAKE) -C drivers/lm75_drv
	$(MAKE) -C drivers/beep_drv
	$(MAKE) -C drivers/mq3_drv
	$(MAKE) -C drivers/adxl345_drv
	$(MAKE) -C drivers/dht11_drv

clean:
	$(MAKE) -C app clean
	$(MAKE) -C drivers/lm75_drv clean
	$(MAKE) -C drivers/beep_drv clean
	$(MAKE) -C drivers/mq3_drv clean
	$(MAKE) -C drivers/adxl345_drv clean
	$(MAKE) -C drivers/dht11_drv clean
