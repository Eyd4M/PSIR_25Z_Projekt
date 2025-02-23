//============= Kod Serwera obslugujacego gry (POSIX) =============
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>


//Parametry gry
#define NUM_GAMES                    100    //rozgrywana liczba gier
#define SEQUENCE_LEN                 4      //dlugosc sekwencji zglaszanej przez graczy
#define MAX_PLAYERS                  3      //maksymalna ilosc zarejestrowanych Klientow
#define WAITING_TIME                 20     //czas poczatkowego oczekiwania na rejestracje Klientow do gry
#define CONFIGURATION_TIME           0.2    //czas na otrzymanie potwierdzenia odebrania kolejnej liczby od Klientow

//Parametry zwi¹zane z gniazdami
#define MAX_BUFF                     3      //bufor do odbioru (i wysylki) maksymalnego rozmiaru wiadomosci protkolu (3 bajty)
#define PORT_NUMBER                  "1204" //numer portu do nas³uchiwania na zgloszenia Klientow UDP
#define PORT_LEN                     48

//Typy wiadomosci
#define REGISTER                     0x80   //1000 0000
#define GAME                         0x40   //0100 0000
#define WINNER                       0x00   //0000 0000
#define END                          0xC0   //1100 0000

//Numery graczy
#define SERVER                       0x00   //0000 0000

//Maski
#define TYPE_MASK                    0xC0   //1100 0000 
#define PLAYER_CODE                  0x38   //0011 1000
#define DATA_MASK                    0x07   //0000 0111


//============= Prototypy funkcji ========================
void make_one_game();                          //Funkcja odpowiedzialna za przeprowadzenie jednej gry
void monitor_sockets();                        //Funkcja odpowiedzialna za monitorowanie socketa w trakcie gry
void send_back_reg(int, int, char[20]);        //Funkcja odpowiadajaca na zgloszenie siê gracza (tresc wiadomosci 10 000 XXX)
void send_sorry(int, char[20]);                //Funkcja wysylajaca Klientowi odmowe dolaczania do gry
void send_data();                              //Funkcja wysylajaca wynik rzutu w danej rundzie
void announce_winner(int);                     //Rozsyla graczom informacje o zwyciezcy fundy, tym samym ja konczac
void send_end();                               //Informowanie graczy o calkowitym koncu rozgrywki gdy skoncza sie wszystkie gry (NUM_GAMES)
uint8_t toss_a_coin();                         //Podrzucenie moneta, zeby uzyskac wynik w danej rundzie
bool check_ack();                              //Funkcja, ktora sprawdza, czy wszyscy gracze sa gotowi na kolejna runde
void hex_to_list(uint16_t, int[SEQUENCE_LEN]); //Funkcja zamieniajaca wartosc hex na liste
float prepare_average();                       //Funkcja obliczajaca srednia ilosc rzutow w danej grze

//============= Deklaracja zmiennych ======================

//struktura danych do przetrzymywania graczy
struct player {
    char ip [20];
    int port;
    int number;
    uint16_t seq;
    int win_counter;
  };
  
struct player connected_players[MAX_PLAYERS];
int num_players = 0;


int rounds_to_win[NUM_GAMES];      //tablica na dane - ile bylo trzeba rund do zwyciestwa w danej grze [10,20,..]
bool is_winner;                    //flaga, czy mamy zwyciezce danej gry
bool has_to_wait_for_players;      //flaga, czy wszyscy gracze potwierdzili wiadomosc z wynikiem rzutu
int currnent_round_counter=0;      //licznik na rundy w trakcie jednej gry
int game_counter;                  //licznik rozegranych gier
int players_with_ack[MAX_PLAYERS]; //tablica na graczy, którzy w danej rundzie potwierdzili odbior wiadomosci
bool registering;                  //flaga, czy serwer moze dalej rejestrowac graczy

//do konfiguracji socketow
struct addrinfo h, *r=NULL;
struct sockaddr_in c;
int s, c_len=sizeof(c);


//Bufory na wiadomosci
unsigned char buffer[MAX_BUFF];
unsigned char msg[MAX_BUFF];


//Inicjalizacja file descriptorow
fd_set master_set, working_set;


//Glowna funkcja aplikacji
int main(){
  printf("\nStarting game's server...\n");

  //Przygotowanie srodowiska do nasluchiwania i wysylania
  memset(&h, 0, sizeof(struct addrinfo));
  h.ai_family=PF_INET;
  h.ai_socktype=SOCK_DGRAM;
  h.ai_flags=AI_PASSIVE;
  if(getaddrinfo(NULL, PORT_NUMBER, &h, &r)!=0){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  if((s=socket(r->ai_family, r->ai_socktype, r->ai_protocol))==-1){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  if(bind(s, r->ai_addr, r->ai_addrlen)!=0){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  
  //Inicjalizacja zbioru deskryptorow
  FD_ZERO(&master_set);
  FD_SET(s, &master_set);
  
  registering = true;
  //Czas na rejestracje wszystkich chetnych graczy
  time_t start_waiting = time(NULL);
  printf("Waiting for players for %d seconds...\n\n", WAITING_TIME);
  while (difftime(time(NULL), start_waiting) < WAITING_TIME) {
    monitor_sockets(); 
  }
  registering = false;
  
  printf("\n=======Starting game============\n");
  srand(time(NULL));
  
  //W petli rozgrywane sa kolejne gry
  for(game_counter = 0; game_counter < NUM_GAMES; game_counter++){ 
    printf("\n\n----Starting %d round!----\n", game_counter + 1);
    make_one_game(); 
  }

  send_end();
  
  printf("\n\n===========================\n");
  printf("==========SUMMARY==========\n");
  printf("===========================\n\n");
  printf("====NUMBERS OF GAMES:%d====\n", game_counter);
  
  float avg = prepare_average();
  
  printf("AVERAGE NUMBER OF ROUNDS: %.1f\n", avg);
  
  for(int player = 0; player < num_players; player++){
    int number = connected_players[player].number;
    int wins = connected_players[player].win_counter;
    float probability = (float) wins/game_counter * 100;
    int list[SEQUENCE_LEN];
    hex_to_list(connected_players[player].seq, list);
    
    printf("\n========PLAYER %d========\n", number);
    printf("Sequence: [");
    for(int i = 0; i < SEQUENCE_LEN-1; i++){
      printf("%d, ", list[i]);
    }
    printf("%d]\n", list[SEQUENCE_LEN-1]);
    printf("Wins: %d\n", wins);
    printf("Probability: %.1f %\n", probability);
  }
  
  freeaddrinfo(r);
  close(s);
  return 0;
}

void make_one_game(){
  is_winner = false;
  has_to_wait_for_players = false;
  currnent_round_counter = 0;
  
  for(;;){
    monitor_sockets();
    if(is_winner){break;}
    
    has_to_wait_for_players = check_ack();
    if(!has_to_wait_for_players){
      send_data();
      memset(players_with_ack, 0, sizeof(players_with_ack));
      has_to_wait_for_players = true;
      currnent_round_counter++;
    }
  }
  printf("----End of %d round!----\n\n", game_counter + 1);
  rounds_to_win[game_counter] = currnent_round_counter;
  

  time_t start_waiting = time(NULL);
  printf("Just a moment for configuration...\n\n");
  while (difftime(time(NULL), start_waiting) < CONFIGURATION_TIME) {
    monitor_sockets(); 
  }
}

float prepare_average(){
  int sum = 0;
  for(int i = 0; i < game_counter; i++){
    sum = sum + rounds_to_win[i];
  }
  return (float) sum/game_counter;
}


void monitor_sockets(){
  FD_ZERO(&working_set);
  memcpy(&working_set, &master_set, sizeof(master_set));

  //akcja z select
  struct timeval timeout = {0, 50000}; // Timeout 50ms
  int activity = select(FD_SETSIZE, &working_set, NULL, NULL, &timeout);
  if (activity < 0) {
        perror("select() error");
        return;
  }
  
  //sprawdzamy polaczenia przychodzace
  if (FD_ISSET(s, &working_set)) {
    int pos=recvfrom(s, buffer, MAX_BUFF, 0, (struct sockaddr*)&c, &c_len);
    if(pos<0){printf("ERROR: %s\n", strerror(errno));exit(-4);}
    if(c_len>0){
      buffer[pos]='\0';
      
      //Obsluga typu 10
      if( ((buffer[0] & TYPE_MASK) == REGISTER)){
        if(registering){
        //Odbior rejestracji gracza
          if((buffer[0] & PLAYER_CODE) == SERVER){
            printf("Received HELLO from (%s:%d)\n", inet_ntoa(c.sin_addr),ntohs(c.sin_port));
            if(num_players < MAX_PLAYERS){
              struct player temp_player;
              temp_player.port = ntohs(c.sin_port);
              strcpy(temp_player.ip, inet_ntoa(c.sin_addr));
              temp_player.number = num_players+1; //numer musi byc o jeden wiekszy od miejsca w tablicy, bo wpisujemy Klientow od 0 miejsca w tablicy
            
              connected_players[num_players] = temp_player;
              send_back_reg(temp_player.number, temp_player.port, temp_player.ip);
              num_players++;
            }else{
              printf("Too many players!!!\n");
              printf("Sending rejection msg!!!\n\n");
              send_sorry(ntohs(c.sin_port), inet_ntoa(c.sin_addr));
            }
          }
          //Odbior sekwencji danego gracza
          else{
            int temp_player_num = (buffer[0] & PLAYER_CODE) >> 3;
            connected_players[temp_player_num-1].seq = buffer[1] << 8 | buffer[2]; //zapisywana sekwencja w strukturze
            connected_players[temp_player_num-1].win_counter = 0;
            players_with_ack[temp_player_num-1] = 1;  //gracz jest gotowy na odbior kolejnej wiadomosci - ROZPOCZECIE GRY
            
            int temp_list[SEQUENCE_LEN];
            hex_to_list(connected_players[temp_player_num-1].seq, temp_list);
            if(sizeof(temp_list)/sizeof(int) == SEQUENCE_LEN){
              printf("Otrzymano sekwencje: [");
              for(int i = 0; i < SEQUENCE_LEN-1; i++){
                printf("%d, ", temp_list[i]);
              }
              printf("%d]\n\n", temp_list[SEQUENCE_LEN-1]);
            }
            else{
              printf("SORRY, YOUR SEQUENCE IS WRONG!!!\n");
              send_sorry(ntohs(c.sin_port), inet_ntoa(c.sin_addr));
            }
          } 
        }
        else{
        printf("The game is pending!!!\n");
        printf("Sending rejection msg!!!\n\n");
        send_sorry(ntohs(c.sin_port), inet_ntoa(c.sin_addr));
        }
      }
      //Obsluga (TYP: 01) - odbieranie wiadomosci ACK od graczy
      else if((buffer[0] & TYPE_MASK) == GAME){
        int temp_number = ((buffer[0] & PLAYER_CODE) >> 3);
        players_with_ack[temp_number-1] = 1;
      }
      
      //Obsluga typu 00
      else if((buffer[0] & TYPE_MASK) == WINNER){
        //Zestawianie nowego polaczenia
        if((buffer[0] & PLAYER_CODE) == SERVER){
          int temp_number = buffer[0] & DATA_MASK;
          players_with_ack[temp_number-1] = 1;
          printf("---- Got RDY from: %x ----\n", temp_number);
          
        }else{
        //Zapisanie i ogloszenie zwyciezcy
          is_winner = true;
          int win_number = ((buffer[0] & PLAYER_CODE) >> 3);
          connected_players[win_number-1].win_counter++;
          announce_winner(win_number);
        }
      }
      
      //Nieznane wiadomosci
      else{
        printf("---- UNKNOWN message: DROPPED ----\n");
      }
    }
  }
}

//Ponizej funkcje odpowiadajace za wysylanie wiadomosci do Klientow
void send_back_reg(int num_player, int port, char ip[20]){
  msg[0] = REGISTER | SERVER | num_player;
  
  char port_str[PORT_LEN];
  snprintf(port_str, PORT_LEN, "%d", port);
  if (getaddrinfo(ip, port_str, &h, &r) != 0) {
      printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
  }
  int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
  if (pos < 0) {
      printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
  }
  if (pos != MAX_BUFF) {
      printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
  }
}

void send_sorry(int port, char ip[20]){
  msg[0] = REGISTER | SERVER;
  
  char port_str[PORT_LEN];
  snprintf(port_str, PORT_LEN, "%d", port);
  if (getaddrinfo(ip, port_str, &h, &r) != 0) {
      printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
  }
  int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
  if (pos < 0) {
      printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
  }
  if (pos != MAX_BUFF) {
      printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
  }
}

void send_data(){ 
  uint8_t result = toss_a_coin();
  printf("Sending data: %d\n", result);
  
  for(int player = 0; player < num_players; player++){
    char ip[20];
    int port = connected_players[player].port;
    strcpy(ip, connected_players[player].ip);
    int num_player = connected_players[player].number;
    
    msg[0] = GAME | (num_player << 3) | result;
    char port_str[PORT_LEN];
    snprintf(port_str, PORT_LEN, "%d", port);
    if (getaddrinfo(ip, port_str, &h, &r) != 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
    if (pos < 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    if (pos != MAX_BUFF) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
  }
}

void announce_winner(int winner){
  printf("ANNOUNCE WINNER... PLAYER: %d !!!\n", winner);
  for(int player = 0; player < num_players; player++){
    char ip[20];
    int port = connected_players[player].port;
    strcpy(ip, connected_players[player].ip);
    int num_player = connected_players[player].number;
    
    msg[0] = WINNER | (num_player << 3) | winner;
    char port_str[PORT_LEN];
    snprintf(port_str, PORT_LEN, "%d", port);
    if (getaddrinfo(ip, port_str, &h, &r) != 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
    if (pos < 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    if (pos != MAX_BUFF) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    
  }
}

void send_end(){
  printf("\n=====END GAME!!!=====\n");
  for(int player = 0; player < num_players; player++){
    char ip[20];
    int port = connected_players[player].port;
    strcpy(ip, connected_players[player].ip);
    int num_player = connected_players[player].number;
    
    msg[0] = END | (num_player << 3);
    char port_str[PORT_LEN];
    snprintf(port_str, PORT_LEN, "%d", port);
    if (getaddrinfo(ip, port_str, &h, &r) != 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
    if (pos < 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    if (pos != MAX_BUFF) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
  }
}

//Ponizej funkcje pomocnicze
uint8_t toss_a_coin(){
  int result = rand() % 2;  
  if (result == 0) {
    return 0x01;  
  }else{
    return 0x00; 
  }
}

bool check_ack(){
  for(int i = 0; i < num_players; i++){
    if(players_with_ack[i] == 0){return true;}
  }
  return false;
}

void hex_to_list(uint16_t hex_value, int list[]){
  uint8_t pos;
  for(pos = 0; pos < SEQUENCE_LEN; pos++){
    list[SEQUENCE_LEN - pos - 1] = hex_value & 0x1;
    hex_value = hex_value >> 1;
  }
}
