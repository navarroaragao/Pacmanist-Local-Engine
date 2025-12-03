#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    int fd = open("levels/1.lv", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }
    
    char buffer[100];
    ssize_t n = read(fd, buffer, 99);
    if (n > 0) {
        buffer[n] = '\0';
        printf("OK: Lido %zd bytes\n", n);
    } else {
        printf("ERRO na leitura\n");
    }
    close(fd);
    return 0;
}
