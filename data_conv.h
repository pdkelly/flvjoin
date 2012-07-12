/* data_conv.c */
double conv_double(const unsigned char *);
unsigned int conv_ui32(const unsigned char *);
unsigned int conv_ui24(const unsigned char *, unsigned char);
unsigned short conv_ui16(const unsigned char *);
short conv_si16(const unsigned char *);

unsigned char *format_double(double);
unsigned char *format_ui32(unsigned int);
unsigned char *format_ui24(unsigned int);
unsigned char *format_ui16(unsigned short);
