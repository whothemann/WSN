CONTIKI_PROJECT = firsttry
all: $(CONTIKI_PROJECT)
	
#UIP_CONF_IPV6=1

CONTIKI = $(HOME)/contiki-ng
MAKE_MAC = MAKE_MAC_CSMA
MAKE_NET = MAKE_NET_NULLNET
MAKE_ROUTING = MAKE_ROUTING_NULLROUTING

MODULES += os/net/nullnet

PROJECT_SOURCEFILES += dio.c dis.c rpl_msg.c rpl_state.c dao.c rpl_route.c msg_up.c msg_down.c trickle.c external_sensors.c

include $(CONTIKI)/Makefile.include
