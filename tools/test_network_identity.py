#!/usr/bin/env python3
"""Compile production DHCP hooks and exercise packet bounds and suffix parsing."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define LWIP_UNUSED_ARG(x) ((void)(x))
#define DHCP_OPTIONS_LEN 312
#define DHCP_OPTION_END 255
#define DHCP_OPTION_PAD 0
#define DHCP_OPTION_PARAMETER_REQUEST_LIST 55
#define DHCP_ACK 5
struct dhcp_msg {u8_t options[DHCP_OPTIONS_LEN];};
struct pbuf {struct pbuf *next;const void *payload;u16_t len;};
static u16_t pbuf_copy_partial(const struct pbuf *p,void *dst,u16_t len,u16_t offset) {
    u16_t copied=0;
    while(p && copied<len) {
        if(offset>=p->len) {offset-=p->len;p=p->next;continue;}
        size_t n=p->len-offset;if(n>(size_t)(len-copied))n=len-copied;
        memcpy((char*)dst+copied,(char*)p->payload+offset,n);
        copied+=n;offset=0;p=p->next;
    }
    return copied;
}
static size_t strlcpy(char *d,const char *s,size_t n) {
    size_t len=strlen(s);if(n){size_t k=len<n-1?len:n-1;memcpy(d,s,k);d[k]=0;}return len;
}
static int default_append, default_parse;
static void dhcp_append_extra_opts(void *n,int state,struct dhcp_msg *msg,u16_t *len)
{(void)n;(void)state;(void)msg;(void)len;default_append++;}
static void dhcp_parse_extra_opts(void *d,int state,int opt,int len,struct pbuf *p,int off)
{(void)d;(void)state;(void)opt;(void)len;(void)p;(void)off;default_parse++;}
'''
HARNESS = r'''
#include "network_identity.c"
static void parse(const char *s, u8_t type) {
    size_t len=strlen(s),split=len/2;
    struct pbuf tail={NULL,s+split,len-split};
    struct pbuf head={&tail,s,split};
    LWIP_HOOK_DHCP_PARSE_OPTION(NULL,NULL,0,NULL,type,15,len,&head,0);
}
int main(void) {
    struct dhcp_msg msg={{53,1,1,55,3,1,3,6,12,1,'x'}};
    u16_t len=11;
    LWIP_HOOK_DHCP_APPEND_OPTIONS(NULL,NULL,0,&msg,1,&len);
    assert(default_append==1 && len==12 && msg.options[4]==4);
    assert(msg.options[8]==15 && msg.options[9]==12 && msg.options[11]=='x');
    panel_dhcp_request_domain(&msg,&len);assert(len==12); /* no duplicates */
    len=DHCP_OPTIONS_LEN-1;panel_dhcp_request_domain(&msg,&len);
    assert(len==DHCP_OPTIONS_LEN-1); /* reserve END byte */
    msg.options[0]=55;msg.options[1]=50;len=4;
    panel_dhcp_request_domain(&msg,&len);assert(len==4); /* malformed */
    char domain[PANEL_DOMAIN_MAX+1];
    parse("SnoogNET.Core.",DHCP_ACK);
    network_identity_get_domain(domain,sizeof(domain));assert(!strcmp(domain,"snoognet.core"));
    parse("offer.example",2);network_identity_get_domain(domain,sizeof(domain));
    assert(!strcmp(domain,"snoognet.core"));
    parse("new.example",DHCP_ACK);network_identity_get_domain(domain,sizeof(domain));
    assert(!strcmp(domain,"new.example")); /* renewal */
    const char *invalid[]={"", ".", "a..b", "-bad.example", "bad-.example", "bad_name", "<script>",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.example"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        parse(invalid[i],DHCP_ACK);network_identity_get_domain(domain,sizeof(domain));assert(!domain[0]);
    }
    const char nul[]={'a',0,'b'};struct pbuf p={NULL,nul,3};
    panel_dhcp_parse_domain(DHCP_ACK,15,3,&p,0);
    network_identity_get_domain(domain,sizeof(domain));assert(!domain[0]);
    p.payload="abc";p.len=3;panel_dhcp_parse_domain(DHCP_ACK,15,9,&p,1);
    network_identity_get_domain(domain,sizeof(domain));assert(!domain[0]);
    parse("valid.example",DHCP_ACK);network_identity_clear_domain();
    network_identity_get_domain(domain,sizeof(domain));assert(!domain[0]);
    assert(default_parse>0);
    puts("DHCP request list, chained packets, renewal, malformed domains and default hook checks passed");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='panel-network-') as directory:
        tmp = Path(directory)
        (tmp/'stubs.h').write_text(STUBS)
        for name in ('freertos/FreeRTOS.h', 'freertos/portmacro.h', 'lwip/arch.h',
                     'lwip/pbuf.h', 'lwip/prot/dhcp.h'):
            path=tmp/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "stubs.h"\n')
        (tmp/'test.c').write_text(HARNESS)
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-I',str(tmp),'-I',str(ROOT/'main'),
                        str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')],check=True)

if __name__ == '__main__':
    main()
