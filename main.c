#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    proxy_ctx_t px;
    if (proxy_init(&px, 8080, 4) != 0) { 
        printf("proxy_init failed\n"); 
        return 1; 
    }
    proxy_run_accept_loop(&px);
    proxy_shutdown(&px);

    return 0;
}
