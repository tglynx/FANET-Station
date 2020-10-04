# FANET setting

#CC=g++
#CFLAGS=-g -c -Wall `mysql_config --cflags` `mysql_config --libs`
CFLAGS=-g -c -Wall

CC=gcc
LIBS=-lwiringPi -lmariadb -lm -lcurl

all: fanet_station

fanet_station: main.o fanet_mysql.o fanet_holfuy.o
	$(CC) main.o  $(LIBS) -o fanet_station
	
main.o: main.c
	$(CC) $(CFLAGS) main.c
	
fanet_mysql.o: fanet_mysql.c
	$(CC) $(CFLAGS) fanet_mysql.c 

fanet_holfuy.o: fanet_holfuy.c
	$(CC) $(CFLAGS) fanet_holfuy.c 

clean:
	rm *.o fanet_station rm *.o 
	
	

