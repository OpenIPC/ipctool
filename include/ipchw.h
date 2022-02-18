#ifndef IPCHW_H
#define IPCHW_H

const char *getchipname();
const char *getchipfamily();
const char *getsensoridentity();
const char *getsensorshort();
char chip_manufacturer[128];
float gethwtemp();


#endif /* IPCHW_H */
