#include "Dispatcher.h"



int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: ./a.out PORT HOSTS_SETTINGS" << std::endl;
        return -1;
    }


    char *file_name = argv[2];
    char *port = argv[1];
    Dispatcher d(port, file_name);

    d.start();
    d.shut_down();
}
