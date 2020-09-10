#ifndef DEVICEIO_FRAMEWORK_SHELL_H_
#define DEVICEIO_FRAMEWORK_shell_H_

#define MSG_BUFF_LEN (10 * 1024) //max size for wifi list

class Shell {
public:
    static bool exec(const char* cmd, char* result, int len);
    static bool scan(const char* cmd, char* result);
    static int pidof(const char *Name);
    static bool system(const char *cmd);
};

#endif //__DEVICEIO_SHELL_H__
