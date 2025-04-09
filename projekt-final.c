#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

//---Zmienne Globalne---//

//PIDy procesow (inicjalizacja wartosciami 1, aby byly rozne od 0 i nie zaklocaly odroznienie procesow)
int P1 = 1, P2 = 1, P3 = 1, PM;

//Zmienne pomagajace polaczyc semafory i sygnaly
int p2_sem3_inter = 0;

//Zmienna (pusta) dla sigqueue
union sigval sigvaldummy;

//Zmienne (przelaczniki) dla odczytywania laczy FIFO
int p1_to_read_fifo = 0;
int p2_to_read_fifo = 0;
int p3_to_read_fifo = 0;

//Zmienna oczekiwania (pauzy) P1
int P1_waiting = 0;

//-----------------------//


//---Handlere sygnalow---//

//Handler dla sygnalow SIGTSTP, SIGCONT, SIGTERM dla -P2-
void p2SCT_hand(int signum, siginfo_t *siginfo, void *dummy) {
    //?if ((siginfo->si_pid) == P1) return;
    //?nie wiem czy trzeba?if ((siginfo->si_pid) == P3) return;
    //?if ((siginfo->si_pid) == PM) return;

    //printf("P2: Otrzymalem sygnal\n");
    //fflush(stdout);

    if (sigqueue(PM, signum, sigvaldummy)) {
        perror("Blad sigqueue w handlerze P2");
    }
    return;
}

void pmSCT_hand(int signum, siginfo_t *siginfo, void *dummy) {
    //Zastepujemy domyslne dzialanie sygnalu pusta funkcja
    return;
}

void p1U1_hand(int signum, siginfo_t *siginfo, void *dummy) {

    //printf("P1: Otrzymalem sygnal %d od %d\n", siginfo->si_signo, siginfo->si_pid);
    //fflush(stdout);

    if ((siginfo->si_pid) == PM) {
        p1_to_read_fifo = 1;
        return;
    }

    return;
}

void p2USR1_hand(int signum, siginfo_t *siginfo, void *dummy) {
    //Handler syglnalu SIGUSR1 w P2

    //printf("P2: Otrzymalem sygnal %d od %d\n", siginfo->si_signo, siginfo->si_pid);
    //fflush(stdout);

    if (siginfo->si_pid == P1) {
        p2_to_read_fifo = 1;
        if (sigqueue(P3, SIGUSR1, sigvaldummy)) {
            perror("Blad sigqueue w hadlerze P2");
        }
        return;
    }

    return;
}

void p3USR1_hand(int signum, siginfo_t *siginfo, void *dummy) {

    //printf("P3: Otrzymalem sygnal %d od %d\n", siginfo->si_signo, siginfo->si_pid);
    //fflush(stdout);

    //Handler syglnalu SIGUSR1 w P3
    if (siginfo->si_pid == P2) {
        p3_to_read_fifo = 1;
        return;
    }

    return;
}

//-----------------------//

int main() {
    //---Inicjowanie zmiennych---//

    //File Deskryptor dla pipe'a

    int pipefd[2];

    //Utworzenie kolejek FIFO
    if (mkfifo("fifoP1", 0600) == -1) perror("Blad utworzenia fifoP1");
    if (mkfifo("fifoP2", 0600) == -1) perror("Blad utworzenia fifoP2");
    if (mkfifo("fifoP3", 0600) == -1) perror("Blad utworzenia fifoP3");

    //Maski dla blokowania sygnalow

    sigset_t maskBlockAll;
    sigset_t maskAllowSCT1;
    sigset_t maskAllowSCT;
    sigset_t maskAllowUSR1;
    sigset_t maskSIGWAITINFOsct1;
    sigset_t maskSIGWAITINFOusr1;
    sigset_t maskAllowCHLD;

    //Maska blokujaca wszystkie sygnaly
    if (sigfillset(&maskBlockAll) == -1) perror("sigfillset error");

    //Maska pozwalajaca na sygnaly SIGTERM, SIGTSTP, SIGCONT, SIGUSR1
    if (sigfillset(&maskAllowSCT1) == -1) perror("sigfillset error");
    if (sigdelset(&maskAllowSCT1, SIGTSTP) == -1) perror("sigdelset error");
    if (sigdelset(&maskAllowSCT1, SIGCONT) == -1) perror("sigdelset error");
    if (sigdelset(&maskAllowSCT1, SIGTERM) == -1) perror("sigdelset error");
    if (sigdelset(&maskAllowSCT1, SIGUSR1) == -1) perror("sigdelset error");

    //Maska pozwalajaca na sygnaly SIGTERM, SIGTSTP, SIGCONT
    if (sigfillset(&maskAllowSCT) == -1) perror("sigfillset error");
    if (sigdelset(&maskAllowSCT, SIGTSTP) == -1) perror("sigdelset error");
    if (sigdelset(&maskAllowSCT, SIGCONT) == -1) perror("sigdelset error");
    if (sigdelset(&maskAllowSCT, SIGTERM) == -1) perror("sigdelset error");

    //Maska pozwalajaca na SIGUSR1
    if (sigfillset(&maskAllowUSR1) == -1) perror("sigfillset error");
    if (sigdelset(&maskAllowUSR1, SIGUSR1) == -1) perror("sigdelset error");


    //Maska pozwalajaca na sygnaly SIGTERM, SIGTSTP, SIGCONT, SIGUSR1 dla !sigwaitinfo!
    if (sigemptyset(&maskSIGWAITINFOsct1) == -1) perror("sigemptyset error");
    if (sigaddset(&maskSIGWAITINFOsct1, SIGTSTP) == -1) perror("sigaddset error");
    if (sigaddset(&maskSIGWAITINFOsct1, SIGTERM) == -1) perror("sigaddset error");
    if (sigaddset(&maskSIGWAITINFOsct1, SIGCONT) == -1) perror("sigaddset error");

    //Maska pozwalajaca na sygnal SIGUSR1 dla !sigwaitinfo!
    if (sigemptyset(&maskSIGWAITINFOusr1) == -1) perror("sigemptyset error");
    if (sigaddset(&maskSIGWAITINFOusr1, SIGUSR1) == -1) perror("sigaddset error");

    //Maska blokujaca wszystkie sygnaly
    if (sigfillset(&maskAllowCHLD) == -1) perror("sigfillset error");
    if (sigdelset(&maskAllowCHLD, SIGCHLD) == -1) perror("sigdelset error");


    //Zestaw semaforow

    int semid, shmid;
    union semun arg, pusty; //Argument dla ustawienia poczatkowego semaforow
    struct sembuf sop, op_sem3_down, op_sem2_down; // Zmienne dla wszystkich operacji na semaforach

    //Ustawienie operacji opuszczenia semafora nr 3
    op_sem3_down.sem_num = 2;
    op_sem3_down.sem_op = -1;
    op_sem3_down.sem_flg = 0;

    //Ustawienie operacji opuszczenia semafora nr 2
    op_sem2_down.sem_num = 1;
    op_sem2_down.sem_op = -1;
    op_sem2_down.sem_flg = 0;

    //-----------------------//

    //---Wstepne operacje dla wszystkich procesow---//

    //Przypisanie numeru pid glownego procesu zmiennej PM
    PM = getpid();

    //Tworzenie zestawow semaforow: 1 - synchronizacja komunikatow
    semid = semget(IPC_PRIVATE, 3, IPC_CREAT | 0600);

    //Ustawienie wartosci 0 na semaforze nr. 1
    arg.val = 0;
    semctl(semid, 0, SETVAL, arg);


    //Ustawienie wartosci 0 na semaforze nr. 3
    arg.val = 0;
    semctl(semid, 2, SETVAL, arg);

    //Ustawienie wartosci 1 na semaforze nr. 2
    arg.val = 1;
    semctl(semid, 1, SETVAL, arg);


    //Tworzenie pamieci wspoldzielonej
    char *shmaddr;
    shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600); // Tworzenie pamieci wspoldzielonej
    if (shmid == -1) {
        perror("Blad shmget");
        exit(1);
    }


    //Otwarcie pipe'a
    if (pipe(pipefd) == -1)
        perror("Blad pipe()");

    //-----------------------//

    //---Tworzenie procesow potomnych---//

    //Tworzenie P3
    P3 = fork();

    if (P3 == -1)
        perror("P3 creation error");

    else if (P3 != 0) {
        // Tworzenie P2
        P2 = fork();

        if (P2 == -1)
            perror("P2 creation error");

        else if (P2 != 0) {
            // Tworzenie P1
            P1 = fork();

            if (P1 == -1)
                perror("P1 creation error");
        }
    }

    //---Ciala procesow P1, P2, P3---//

    if (P1 == 0) {
        // Cialo P1

        //---Zmienne dla P1---//
        char choise[3] = {"3\n"};
        FILE *fileRead;
        char *line;
        unsigned long lenbuf = 0;
        int P1_run_reading = 1, P1_menu_run = 1, P1_run_term = 1, P1_to_term = 0;
        int sig_fifo;
        char linestd[128] = {'\0'};
        siginfo_t sig_inf;
        char *fgets_return;
        char *fgets_return_menu;
        char end_sequence[2] = {".\n"};
        int semop_return = 0;

        //---Wstepne operacje---//

        //Zamkniecie koncow pipe'a
        close(pipefd[0]);
        close(pipefd[1]);

        //Otwarcie FIFO dla P1 do odczytu
        int fifoP1fd = open("fifoP1", O_RDONLY);
        if (fifoP1fd == -1) perror("Blad otwarcia fifoP1");

        // Dolaczenie pamieci wspoldzielonej
        shmaddr = shmat(shmid, NULL, 0);
        if (shmaddr == (void *) -1) {
            perror("Blad shmat");
            exit(1);
        }


        //Ustawienie maski sygnalow na start
        if (sigprocmask(SIG_SETMASK, &maskAllowUSR1, NULL) == -1) perror("Blad sigprocmask na start w P1");

        //Uruchomienie handlera sygnalu SIGUSR1
        struct sigaction sigactU1;
        sigactU1.sa_sigaction = &p1U1_hand;
        sigactU1.sa_flags = SA_SIGINFO;

        sigaction(SIGUSR1, &sigactU1, NULL);

        //printf("witaj z P1\n");


        //Czekamy az PM podniesie semafor nr 1 po wypisaniu danych o numerach procesow
        sop.sem_num = 0;
        sop.sem_op = -1;
        sop.sem_flg = 0;
        semop(semid, &sop, 1);

        //Wyswietlenie informacji o procesach
        //printf("P1: P1: %d  P2: %d   P3: %d\n\n", P1, P2, P3);
        //fflush(stdout);

        while (P1_menu_run == 1) {
            //Podanie trybu dzialania

            //Semafor wypisania P3
            semop(semid, &op_sem2_down, 1);

            do{
                printf("1.Czytaj plik\n2.Czytaj terminal\n->");
                strcpy(choise,"0\n");
                fgets_return_menu = fgets(choise,3, stdin);
                //printf("%s",choise);

                //Sprawdzenie czy musimy odczytac FIFO (po ewentualnym odbiorze sygnalu)
                if (p1_to_read_fifo) {

                    if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                    perror("Blad sigqueue w hadlerze P1");
                    }

                    // Odczytanie numeru sygnalu z FIFO
                    if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                        perror("read from fifoP1 blad");

                    //printf("P1: Odczytano z FIFO na poczatku P1: %d\n", sig_fifo);
                    //fflush(stdout);

                    //Po odczytaniu opuszczamy flage
                    p1_to_read_fifo = 0;

                    if (sig_fifo == SIGTSTP) {

                        P1_waiting = 1;

                        while (P1_waiting) {
                            // Petla pauzy procesu P1

                            //printf("P1 Waiting...\n");
                            //fflush(stdout);

                            // Oczekiwanie na sygnal SIGUSR1
                            if (sigwaitinfo(&maskSIGWAITINFOusr1, &sig_inf) == -1)
                                perror("Blad sigwaitinfo P1");

                            // printf("P1 Otrzymano jakis sygnal... i p1_to_read_fifo = %d\n", );
                            // fflush(stdout);

                            // Jezeli byl od PM to odczytaj FIFO
                            if (sig_inf.si_pid == PM) {
                                //printf("P1 Otrzymano sygnal podczas czekania od PM...\n");
                                //fflush(stdout);

                                if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                    perror("Blad sigqueue w hadlerze P1");
                                }

                                if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                    perror("read from fifoP1 blad");

                                //printf("P1: Podczas czekania odczytano z FIFO sig: %d\n", sig_fifo);
                                //fflush(stdout);


                                if (sig_fifo == SIGCONT)
                                    P1_waiting = 0; // Jezeli otrzymalismy polecenie kontynuacji to wyjdz z czekania

                                else if (sig_fifo == SIGTERM) {
                                    // Jesli mamy sie zatrzymac to konczymy dzialanie
                                    P1_waiting = 0;
                                    P1_menu_run = 0;
                                }
                            }
                        }
                    } else if (sig_fifo == SIGTERM) {
                        // Jesli mamy sie zatrzymac to konczymy dzialanie
                        //printf("P1: Koncze dzialanie run_term = 0\n");
                        //fflush(stdout);
                        P1_menu_run = 0;
                    }
                }

            }while(fgets_return_menu == NULL && errno == EINTR && P1_menu_run == 1);

            if (strncmp("1\n",choise,2) == 0) {
                //Czytanie z pliku
                P1_run_reading = 1;

                //Ustawienie wartosci 1 na semaforze nr. 2
                arg.val = 1;
                semctl(semid, 1, SETVAL, arg);

                //Otwarcie pliku
                fileRead = fopen("lines.txt", "r");
                if (fileRead == NULL) {
                    perror("Blad otwarcia pliku");
                    exit(1);
                }

                while (P1_run_reading) {


                    //Czekamy az P3 podniesie semafor nr 2 po pomyslnym wypisaniu dlugosci na terminal
                    do{
                        semop_return = semop(semid, &op_sem2_down, 1);

                        //printf("p1_to_read_fifo = %d\n",p1_to_read_fifo);
                        //fflush(stdout);

                        //Sprawdzenie czy musimy odczytac FIFO (po ewentualnym odbiorze sygnalu)
                        if (p1_to_read_fifo) {
                            // Odczytanie numeru sygnalu z FIFO
                            if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                perror("read from fifoP1 blad");

                            //printf("P1: Odczytano z FIFO: %d\n", sig_fifo);
                            //fflush(stdout);

                            //Po odczytaniu opuszczamy flage
                            p1_to_read_fifo = 0;

                            if (sig_fifo == SIGTSTP) {

                                if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                perror("Blad sigqueue w hadlerze P1");
                                }
                                P1_waiting = 1;

                                while (P1_waiting) {
                                    // Petla pauzy procesu P1

                                    //printf("P1 Waiting...\n");
                                    //fflush(stdout);

                                    // Oczekiwanie na sygnal SIGUSR1
                                    if (sigwaitinfo(&maskSIGWAITINFOusr1, &sig_inf) == -1)
                                        perror("Blad sigwaitinfo P1");

                                    // printf("P1 Otrzymano jakis sygnal... i p1_to_read_fifo = %d\n", );
                                    // fflush(stdout);

                                    // Jezeli byl od PM to odczytaj FIFO
                                    if (sig_inf.si_pid == PM) {
                                        //printf("P1 Otrzymano sygnal podczas czekania od PM...\n");
                                        //fflush(stdout);

                                        if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                            perror("read from fifoP1 blad");

                                        //printf("P1: Podczas czekania odczytano z FIFO sig: %d\n", sig_fifo);
                                        //fflush(stdout);

                                        if (sig_fifo == SIGCONT){
                                            if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                            perror("Blad sigqueue w hadlerze P1");
                                            }
                                            P1_waiting = 0; // Jezeli otrzymalismy polecenie kontynuacji to wyjdz z czekania

                                        }else if (sig_fifo == SIGTERM) {
                                            // Jesli mamy sie zatrzymac to konczymy dzialanie
                                            P1_waiting = 0;
                                            P1_menu_run = 0;
                                            P1_to_term = 1;
                                        }
                                    }
                                }
                            } else if (sig_fifo == SIGTERM) {
                                // Jesli mamy sie zatrzymac to konczymy dzialanie
                                P1_menu_run = 0;
                                P1_to_term = 1;
                            }
                        }
                    }while(semop_return == -1 && errno == EINTR && P1_run_reading == 1);


                    //Jezeli podczas czekania otrzymalismy sygnal zatrzymania SIGTERM, to zakoncz dzialanie
                    if (P1_run_reading != 0) {


                        //Ustawienie maski dla sekcji krytycznej
                        if (sigprocmask(SIG_SETMASK, &maskBlockAll, NULL) == -1)
                            perror("Blad sigprocmask wejscie do sek. kryt");

                        // Odczyt linii po linii dopoki nie osiagniemy konca
                        if (getline(&line, &lenbuf, fileRead) == -1) P1_run_reading = 0;


                        // Zapis do pamieci wspoldzielonej wczytanej linii
                        strcpy(shmaddr, line);
                        // printf("Zapisano do shared memory\n");
                        // fflush(stdout);

                        // Podniesienie semafora nr. 3 po wpisaniu odczytanej linii do shared memory

                        sop.sem_num = 2;
                        sop.sem_op = 1;
                        sop.sem_flg = 0;
                        semop(semid, &sop, 1);

                        //Wyjscie z sekcji kryt. ustawiamy maske z powrotem
                        if (sigprocmask(SIG_SETMASK, &maskAllowUSR1, NULL) == -1)
                            perror("Blad sigprocmask wejscie do sek. kryt");

                        //Sprawdzenie czy musimy odczytac FIFO (po ewentualnym odbiorze sygnalu)
                        if (p1_to_read_fifo) {
                            // Odczytanie numeru sygnalu z FIFO
                            if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                perror("read from fifoP1 blad");

                            //printf("P1: Odczytano z FIFO: %d\n", sig_fifo);
                            //fflush(stdout);

                            //Po odczytaniu opuszczamy flage
                            p1_to_read_fifo = 0;

                            if (sig_fifo == SIGTSTP) {

                                if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                perror("Blad sigqueue w hadlerze P1");
                                }
                                P1_waiting = 1;

                                while (P1_waiting) {
                                    // Petla pauzy procesu P1

                                    //printf("P1 Waiting...\n");
                                    //fflush(stdout);

                                    // Oczekiwanie na sygnal SIGUSR1
                                    if (sigwaitinfo(&maskSIGWAITINFOusr1, &sig_inf) == -1)
                                        perror("Blad sigwaitinfo P1");

                                    // printf("P1 Otrzymano jakis sygnal... i p1_to_read_fifo = %d\n", );
                                    // fflush(stdout);

                                    // Jezeli byl od PM to odczytaj FIFO
                                    if (sig_inf.si_pid == PM) {
                                        //printf("P1 Otrzymano sygnal podczas czekania od PM...\n");
                                        //fflush(stdout);

                                        if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                            perror("read from fifoP1 blad");

                                        //printf("P1: Podczas czekania odczytano z FIFO sig: %d\n", sig_fifo);
                                        //fflush(stdout);

                                        if (sig_fifo == SIGCONT){
                                            if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                            perror("Blad sigqueue w hadlerze P1");
                                                }
                                            P1_waiting = 0; // Jezeli otrzymalismy polecenie kontynuacji to wyjdz z czekania

                                        }else if (sig_fifo == SIGTERM) {
                                            // Jesli mamy sie zatrzymac to konczymy dzialanie
                                            P1_waiting = 0;
                                            P1_menu_run = 0;
                                            P1_to_term = 1;
                                        }
                                    }
                                }
                            } else if (sig_fifo == SIGTERM) {
                                // Jesli mamy sie zatrzymac to konczymy dzialanie
                                P1_menu_run = 0;
                                P1_to_term = 1;
                            }
                        }
                    }
                }

                if(P1_to_term){
                    if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                    perror("Blad sigqueue w P1");
                        }
                }

            }
            else if (strncmp("2\n",choise,2) == 0) {
                // Czytanie z STDIN
                P1_run_term = 1;
                //Ustawienie wartosci 1 na semaforze nr. 2
                arg.val = 1;
                semctl(semid, 1, SETVAL, arg);

                while (P1_run_term) {
                    //Opuszcznie semafora nr.2, ktory podnosi P3
                    if (semop(semid, &op_sem2_down, 1) == -1) perror("Blad opuszczenia semafora 2 w P1 - terminal");


                    do{
                        printf(">");
                        fgets_return = fgets(linestd, 128, stdin);

                        if(strncmp(linestd,end_sequence,2) == 0) P1_run_term = 0;

                        //Sprawdzenie czy musimy odczytac FIFO (po ewentualnym odbiorze sygnalu)
                        if (p1_to_read_fifo) {

                            if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                perror("Blad sigqueue w hadlerze P1");
                                }
                            // Odczytanie numeru sygnalu z FIFO
                            if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                perror("read from fifoP1 error");

                            //printf("P1: Odczytano z FIFO na poczatku P1: %d\n", sig_fifo);
                            //fflush(stdout);

                            //Po odczytaniu opuszczamy flage
                            p1_to_read_fifo = 0;

                            if (sig_fifo == SIGTSTP) {

                                P1_waiting = 1;

                                while (P1_waiting) {
                                    // Petla pauzy procesu P1

                                    //printf("P1 Waiting...\n");
                                    //fflush(stdout);

                                    // Oczekiwanie na sygnal SIGUSR1
                                    if (sigwaitinfo(&maskSIGWAITINFOusr1, &sig_inf) == -1)
                                        perror("Blad sigwaitinfo P1");

                                    // printf("P1 Otrzymano jakis sygnal... i p1_to_read_fifo = %d\n", );
                                    // fflush(stdout);

                                    // Jezeli byl od PM to odczytaj FIFO
                                    if (sig_inf.si_pid == PM) {
                                        //printf("P1 Otrzymano sygnal podczas czekania od PM...\n");
                                        //fflush(stdout);

                                        if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                            perror("Blad sigqueue w hadlerze P1");
                                        }

                                        if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                            perror("read from fifoP1 blad");

                                        //printf("P1: Podczas czekania odczytano z FIFO sig: %d\n", sig_fifo);
                                        //fflush(stdout);


                                        if (sig_fifo == SIGCONT)
                                            P1_waiting = 0; // Jezeli otrzymalismy polecenie kontynuacji to wyjdz z czekania

                                        else if (sig_fifo == SIGTERM) {
                                            // Jesli mamy sie zatrzymac to konczymy dzialanie
                                            P1_waiting = 0;
                                            P1_run_term = 0;
                                            P1_menu_run = 0;
                                        }
                                    }
                                }
                            } else if (sig_fifo == SIGTERM) {
                                // Jesli mamy sie zatrzymac to konczymy dzialanie
                                //printf("P1: Koncze dzialanie run_term = 0\n");
                                //fflush(stdout);
                                P1_menu_run = 0;
                                P1_run_term = 0;
                            }
                        }

                    }while(fgets_return == NULL && errno == EINTR && P1_run_term == 1);


                    //Jezeli podczas czekania otrzymalismy sygnal zatrzymania SIGTERM, to zakoncz dzialanie
                    if (P1_run_term != 0) {

                        //Ustawienie maski dla sekcji krytycznej
                        if (sigprocmask(SIG_SETMASK, &maskBlockAll, NULL) == -1)
                            perror("Blad sigprocmask wejscie do sek. kryt");

                        // Zapis do pamieci wspoldzielonej pobranej linii z terminala
                        strcpy(shmaddr, linestd);
                        //printf("Zapisano do shmem %s ", linestd);
                        //fflush(stdout);

                        // Podniesienie semafora nr. 3 po wpisaniu odczytanej linii do shared memory

                        sop.sem_num = 2;
                        sop.sem_op = 1;
                        sop.sem_flg = 0;
                        semop(semid, &sop, 1);

                        //Wyjscie z sekcji kryt. ustawiamy maske z powrotem
                        if (sigprocmask(SIG_SETMASK, &maskAllowUSR1, NULL) == -1)
                            perror("Blad sigprocmask wejscie do sek. kryt");

                        //Sprawdzenie czy musimy odczytac FIFO (po ewentualnym odbiorze sygnalu)
                        if (p1_to_read_fifo) {
                            if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                            perror("Blad sigqueue w hadlerze P1");
                                                }

                            //printf("P1: Otrzymalem zlecenie czytac FIFO na koncu\n");
                            //fflush(stdout);
                            // Odczytanie numeru sygnalu z FIFO
                            if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                perror("read from fifoP1 blad");

                            //printf("P1: Odczytano z FIFO: %d\n", sig_fifo);
                            //fflush(stdout);

                            //Po odczytaniu opuszczamy flage
                            p1_to_read_fifo = 0;

                            if (sig_fifo == SIGTSTP) {

                                P1_waiting = 1;

                                while (P1_waiting) {
                                    // Petla pauzy procesu P1

                                    //printf("P1 Waiting...\n");
                                    //fflush(stdout);

                                    // Oczekiwanie na sygnal SIGUSR1
                                    if (sigwaitinfo(&maskSIGWAITINFOusr1, &sig_inf) == -1)
                                        perror("Blad sigwaitinfo P1");

                                    // printf("P1 Otrzymano jakis sygnal... i p1_to_read_fifo = %d\n", );
                                    // fflush(stdout);

                                    // Jezeli byl od PM to odczytaj FIFO
                                    if (sig_inf.si_pid == PM) {
                                        //printf("P1 Otrzymano sygnal podczas czekania od PM...\n");
                                        //fflush(stdout);

                                        if (sigqueue(P2, SIGUSR1, sigvaldummy)) {
                                            perror("Blad sigqueue w hadlerze P1");
                                        }

                                        if (read(fifoP1fd, &sig_fifo, sizeof(int)) == -1)
                                            perror("read from fifoP1 blad");

                                        //printf("P1: Podczas czekania odczytano z FIFO sig: %d\n", sig_fifo);
                                        //fflush(stdout);


                                        if (sig_fifo == SIGCONT)
                                            P1_waiting = 0; // Jezeli otrzymalismy polecenie kontynuacji to wyjdz z czekania

                                        else if (sig_fifo == SIGTERM) {
                                            // Jesli mamy sie zatrzymac to konczymy dzialanie
                                            P1_waiting = 0;
                                            P1_run_term = 0;
                                            P1_menu_run = 0;
                                        }
                                    }
                                }
                            }
                        } else if (sig_fifo == SIGTERM) {
                            // Jesli mamy sie zatrzymac to konczymy dzialanie
                            P1_menu_run = 0;
                            P1_run_term = 0;
                        }
                    }

                    //Ustawienie wartosci 1 na semaforze nr. 2
                    arg.val = 1;
                    semctl(semid, 1, SETVAL, arg);
                }


            }
            else if(P1_menu_run == 1) {
                printf("Bledny wybor\n");
                fflush(stdout);
            }

        }

        //printf("P1: Zakonczylem dzialanie\n");
        //fflush(stdout);

        //---Czyszczenie po sobie---//

        if (shmdt(shmaddr) == -1) perror("P1: Blad detach shmem");
        if (close(fifoP1fd) == -1) perror("P1: Blad close fifoP1fd");

        exit(0);
    }

    if (P2 == 0) {
        // Cialo P2

        //printf("witaj z P2\n");

        //---Zmienne dla P2---//
        char line[4096] = {'\0'};
        int linelength;
        int P2run = 1;
        int sig_fifo;
        int semop_return = 0;

        //---Wstepne operacje---//

        //Zamkniecie konca pipe'a do odczytu, bo bedziemy pisac
        close(pipefd[0]);

        //Otwarcie FIFO dla P2 do odczytu
        int fifoP2fd = open("fifoP2", O_RDONLY);
        if (fifoP2fd == -1) perror("Blad otwarcia fifoP1");

        //Czekamy az PM podniesie semafor nr 1 po wypisaniu danych o numerach procesow
        sop.sem_num = 0;
        sop.sem_op = -1;
        sop.sem_flg = 0;
        if (semop(semid, &sop, 1) == -1) perror("Blad opuszczenia semafora nr1");

        if (read(fifoP2fd, &P1, sizeof(int)) == -1) perror("Blad read z FIFO pidu P1");

        //Wyswietlenie informacji o procesach
        //printf("P2: P1: %d  P2: %d   P3: %d\n\n", P1, P2, P3);
        //fflush(stdout);

        // Dolaczenie pamieci wspoldzielonej
        shmaddr = shmat(shmid, NULL, 0);
        if (shmaddr == (void *) -1) {
            perror("Blad shmat()");
            exit(1);
        }

        //Ustawienie poczatkowej maski sygnalow
        if (sigprocmask(SIG_SETMASK, &maskAllowSCT1, NULL) == -1) perror("sigprocmask error");

        struct sigaction sigactSCT;
        sigactSCT.sa_sigaction = &p2SCT_hand;
        sigactSCT.sa_flags = SA_SIGINFO;

        sigaction(SIGTSTP, &sigactSCT, NULL);
        sigaction(SIGCONT, &sigactSCT, NULL);
        sigaction(SIGTERM, &sigactSCT, NULL);

        struct sigaction sigactU1;
        sigactU1.sa_sigaction = &p2USR1_hand;
        sigactU1.sa_flags = SA_SIGINFO;

        sigaction(SIGUSR1, &sigactU1, NULL);


        //---Glowna petla operacji---//

        while (P2run == 1) {

            //Probujemy opuscic semafor nr.3 aby odczytac shared memory, dopoki nie uda nam sie go opuscic
            //(sygnal moze zaklocic opuszczanie)
            do{
                semop_return = semop(semid, &op_sem3_down, 1);

                //Podczas czekania sprawdzamy czy powinnismy odczytac FIFO
                if (p2_to_read_fifo) {
                    // Odczytanie numeru sygnalu z FIFO
                    if (read(fifoP2fd, &sig_fifo, sizeof(int)) == -1)
                        perror("read from fifoP2 blad");
                    p2_to_read_fifo = 0;

                    if (sig_fifo == SIGTERM) {
                        P2run = 0;
                    }
                }
            }while(semop_return == -1 && errno == EINTR && P2run == 1);

            //Jezeli otrzymalismy SIGTSTP to przerwij petle
            if (P2run != 0) {


                //Zablokowanie wszystkich sygnalow, wchodzimy do sekcji krytycznej
                sigprocmask(SIG_SETMASK, &maskBlockAll, NULL);

                //Skopiowanie linii do zmiennej line
                strcpy(line, shmaddr);

                //Usuniecie napisu z pamieci wspoldzielonej
                strcpy(shmaddr, "");

                //printf("Otrzymalem: %s\n",line);
                //fflush(stdout);

                //Obliczenie dlugosci ciagu, uwzgledniajac ze na koncu mamy '\n'
                linelength = strlen(line) - 1;

                //Wyslanie dlugosci ciagu do P3 przez pipe'a
                write(pipefd[1], &linelength, sizeof(int));

                //Wyjscie z sekcji krytycznej, odblokowanie sygnalow
                sigprocmask(SIG_SETMASK, &maskAllowSCT1, NULL);

                if (p2_to_read_fifo) {
                    // Odczytanie numeru sygnalu z FIFO
                    if (read(fifoP2fd, &sig_fifo, sizeof(int)) == -1)
                        perror("read from fifoP2 blad");
                    p2_to_read_fifo = 0;

                    if (sig_fifo == SIGTERM) {
                        P2run = 0;
                    }
                }
            }
        }

        //---Czyszczenie po sobie---//

        if (shmdt(shmaddr) == -1) perror("P2: Blad detach shmdt");
        if (close(fifoP2fd) == -1) perror("P2: Blad zamk. fifoP2fd");
        if (close(pipefd[1]) == -1) perror("P2: Blad zamk. pipefd[1]");

        exit(0);
    }

    if (P3 == 0) {
        // Cialo P3

        //---Zmienne dla P3--//

        int lenline;
        int P3run = 1;
        int sig_fifo;
        int read_return = 0;

        //---Wstepne operacje---//

        //Zamkniecie konca pipe'a do pisania, bo bedziemy czytac
        close(pipefd[1]);

        //Otwarcie FIFO dla P3 do odczytu
        int fifoP3fd = open("fifoP3", O_RDONLY);
        if (fifoP3fd == -1) perror("Blad otwarcia fifoP3");

        //Czekamy az PM podniesie semafor nr 1 po wypisaniu danych o numerach procesow
        sop.sem_num = 0;
        sop.sem_op = -1;
        sop.sem_flg = 0;
        if (semop(semid, &sop, 1) == -1) perror("Blad opuszczenia semafora nr1");

        if (read(fifoP3fd, &P2, sizeof(int)) == -1) perror("Blad read z FIFO pidu P2");
        if (read(fifoP3fd, &P1, sizeof(int)) == -1) perror("Blad read z FIFO pidu P1");

        //Wyswietlenie informacji o procesach
        //printf("P3: P1: %d  P2: %d   P3: %d\n\n", P1, P2, P3);
        //fflush(stdout);

        //Ustawienie maski sygnalow
        sigprocmask(SIG_SETMASK, &maskAllowUSR1, NULL);

        //Podpiecie handlera sygnalu SIGUSR1
        struct sigaction sigactU1;
        sigactU1.sa_sigaction = &p3USR1_hand;
        sigactU1.sa_flags = SA_SIGINFO;

        sigaction(SIGUSR1, &sigactU1, NULL);

        //---Glowna petla operacji---//


        while (P3run == 1) {

            //Odczytanie dlugosci ciagu z pipe'a, az sie uda, bo podczas czekania moze przyjsc sygnal
            do {
                read_return = read(pipefd[0], &lenline, sizeof(int));

                //Jezeli otrzymalismy sygnal od P2 i musimy odczytac FIFO to robimy to
                if (p3_to_read_fifo) {

                    //printf("P3: Odczyt FIFO podczas read\n");
                    //fflush(stdout);

                    //Odczyt FIFO w P3
                    if (read(fifoP3fd, &sig_fifo, sizeof(int)) == -1) perror("Blad read z FIFO w P3");

                    p3_to_read_fifo = 0;

                    //Sprawdzenie sygnalu czy jest SIGTERM i jezeli tak to zatrzymaj proces
                    if (sig_fifo == SIGTERM) {
                        P3run = 0;
                    }

                }
            } while((read_return == -1 || read_return == 0) && P3run == 1 );

            //Jezeli otrzymalismy SIGTERM to przerwij glowna petle
            if (P3run != 0) {

                //Ustawienie maski dla sekcji krytycznej
                if (sigprocmask(SIG_SETMASK, &maskBlockAll, NULL) == -1)
                    perror("Blad sigprocmask wejscie do sek. kryt");

                //Wywietlenie dlugosci ciag w terminale
                printf("P3: %d\n", lenline);
                fflush(stdout);

                //Podniesienie semafora nr.2 aby P1 mogl odczytac nastepna linie
                sop.sem_num = 1;
                sop.sem_op = 1;
                sop.sem_flg = 0;
                semop(semid, &sop, 1);

                //Ustawienie maski po sekcji krytycznej
                if (sigprocmask(SIG_SETMASK, &maskAllowUSR1, NULL) == -1)
                    perror("Blad sigprocmask wejscie do sek. kryt");

                //Jezeli otrzymalismy sygnal od P2 i musimy odczytac FIFO to robimy to
                if (p3_to_read_fifo) {

                    //printf("P3: Odczyt FIFO na koncu\n");
                    //fflush(stdout);

                    //Odczyt FIFO w P3
                    if (read(fifoP3fd, &sig_fifo, sizeof(int)) == -1) perror("Blad read z FIFO w P3");

                    p3_to_read_fifo = 0;

                    //Sprawdzenie sygnalu czy jest SIGTERM i jezeli tak to zatrzymaj proces
                    if (sig_fifo == SIGTERM) {
                        P3run = 0;
                        break;
                    }

                }
            }
        }

        //printf("P3: Zakonczylem dzialanie\n");
        //fflush(stdout);
        //---Czyszczenie po sobie---//

        if (close(fifoP3fd) == -1) perror("P3: Blad close fifoP3fd");
        if (close(pipefd[0]) == -1) perror("P3: Blad close pipefd[0]");

        exit(0);
    }


    //Cialo PM

    //---Zmienne dla -PM- ---//

    int PM_run = 1;
    siginfo_t signal_info_pm;
    int signum;

    //--------------------------//

    //Zablokowanie sygnalow oprocz potrzebnych
    if (sigprocmask(SIG_SETMASK, &maskAllowSCT, NULL) == -1) perror("sigprocmask error");

    //Ustawienie handlera sygnalow
    struct sigaction sigactSCT;
    sigactSCT.sa_sigaction = &pmSCT_hand;
    sigactSCT.sa_flags = SA_SIGINFO;

    sigaction(SIGTSTP, &sigactSCT, NULL);
    sigaction(SIGCONT, &sigactSCT, NULL);
    sigaction(SIGTERM, &sigactSCT, NULL);


    //Zamkniecie koncow pipe'a
    close(pipefd[0]);
    close(pipefd[1]);

    //Otwarcie kolejek FIFO do zapisu
    int fifoP1fd = open("fifoP1", O_WRONLY);
    if (fifoP1fd == -1) perror("Blad otwarcia fifoP1");

    int fifoP2fd = open("fifoP2", O_WRONLY);
    if (fifoP2fd == -1) perror("Blad otwarcia fifoP1");

    int fifoP3fd = open("fifoP3", O_WRONLY);
    if (fifoP3fd == -1) perror("Blad otwarcia fifoP1");

    //Wyswietlenie informacji o procesach
    printf("PM: %d  P1: %d  P2: %d   P3: %d\n\n", PM, P1, P2, P3);
    fflush(stdout);

    //Zapis pidow procesow do tych ktore nie maja
    if (write(fifoP3fd, &P2, sizeof(int)) == -1) perror("Blad zapisu pidu P2 do FIFO P3");
    if (write(fifoP3fd, &P1, sizeof(int)) == -1) perror("Blad zapisu pidu P1 do FIFO P3");

    if (write(fifoP2fd, &P1, sizeof(int)) == -1) perror("Blad zapisu pidu P1 do FIFO P2");


    sop.sem_num = 0;
    sop.sem_op = 3;
    sop.sem_flg = 0;
    semop(semid, &sop, 1); //Podniesienie semafora, ze juz mozna zaczac procesy P1, P2 i P3


    //---Petla procesu PM---//

    while (PM_run == 1) {

        //printf("PM: wszedlem do petli\n");
        //fflush(stdout);

        //Czekanie na sygnal i zapisanie jego wartosci w zmiennej signum a info o sygnale w signal_info_pm
        signum = sigwaitinfo(&maskSIGWAITINFOsct1, &signal_info_pm);

        //printf("PM: otrzymalem sygnal %d od %d\n", signum, signal_info_pm.si_pid);
        //fflush(stdout);

        // Jezeli nadawca to P2 to obsluz sygnal
        if (signal_info_pm.si_pid == P2) {
            //printf("Zapis sygnalow do FIFO\n");
            //Umieszczenie numeru sygnalu w laczach FIFO
            if (write(fifoP1fd, &signum, sizeof(int)) == -1)
                perror("Blad zapisu do fifoP1");
            if (write(fifoP2fd, &signum, sizeof(int)) == -1)
                perror("Blad zapisu do fifoP2");
            if (write(fifoP3fd, &signum, sizeof(int)) == -1)
                perror("Blad zapisu do fifoP3");

            //Wyslanie sygnalu SIGUSR1 do P1
            if (sigqueue(P1, SIGUSR1, sigvaldummy))
                perror("Blad sigqueue w PM");

            if (signum == SIGTERM) PM_run = 0;
        }
    }
    //--------------------------//

    //Odblokowanie tylko SIGCHLD
    if (sigprocmask(SIG_SETMASK, &maskAllowCHLD, NULL) == -1) perror("sigprocmask error");

    while (wait(NULL) != -1);

    //Czyszczenie po programie

    if (close(fifoP1fd) == -1) perror("Blad zamk. fifoP1");
    if (close(fifoP2fd) == -1) perror("Blad zamk. fifoP2");
    if (close(fifoP3fd) == -1) perror("Blad zamk. fifoP3");

    if (unlink("fifoP1") == -1) perror("Blad unlink fifoP1");
    if (unlink("fifoP2") == -1) perror("Blad unlink fifoP2");
    if (unlink("fifoP3") == -1) perror("Blad unlink fifoP3");

    if (shmctl(shmid, IPC_RMID, NULL) == -1) perror("Blad usuniecia Shared MEM");

    if (semctl(semid, 0, IPC_RMID, pusty) == -1) perror("Blad usuniecia semaforow");


    //printf("PM: %d", PM);
    //fflush(stdout);
    //pause();
    return 0;
}
