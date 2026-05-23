#ifndef HEAD_H
#define HEAD_H


#include <MQTTAsync.h>
#include <MQTTClient.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <string.h>
//#define OLD_ADDRESS     "tcp://218.201.45.7:1883"
#define NEW_ADDRESS     "tcp://183.230.40.96:1883"
#define DEV_NAME    "device_01"
#define CLIENTID DEV_NAME
#define PRODUCT_ID "6x3XZH6Gv4"
#define PASSWD "version=2018-10-31&res=products%2F6x3XZH6Gv4%2Fdevices%2Fdevice_01&et=1837255523&method=md5&sign=0jJNfNUIug3KwvrvlWrJVg%3D%3D";
#define QOS         0
#define TIMEOUT     10000L
//#define __cplusplus，，将这上面的MQTT信息添加到代码里面 
#endif // HEAD_H
