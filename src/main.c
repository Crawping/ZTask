#include "ztask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>




int
main(int argc, char *argv[]) {
    ztask_init();
    struct ztask_config config = { 0 };
    config.thread =  8;
    config.module_path = "./?.dll";
    config.harbor = 1;
    config.daemon = NULL;
    config.logger =NULL;
    config.logservice = "logger";
    config.profile = 1;
    config.bootstrap = "snc";
    struct ztask_snc boot;
    boot._cb;
    config.bootstrap_parm = &boot;
    config.bootstrap_parm_sz = sizeof(struct ztask_snc);


    ztask_start(&config);
    ztask_uninit();
    return 0;
}
