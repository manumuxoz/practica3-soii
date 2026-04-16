#include <pthread.h> // hilos
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// 3 productores y 1 consumidor son hilos dentro del mismo proceso
// al compartir el espacio de direcciones la estructura compartida
// es una variable global (no se necesita un archivo de memoria
// compartida para mapear en memoria)
// empleamos mutexes y variables de condición para solucionar
// las carreras criticas
// los items pertenecen a diferentes colas de prioridad dependiendo de quien los
// haya producido
// los items tienen un tiempo de creacion y caducidad asociado
// si el tiempo transcurrido es mayor al tiempo de caducidad
// no se tiene en cuenta para las sumas acumuladas en el consumidor

#define N 10           // tamaño del buffer
#define NUM_PROD 3     // numero de productores
#define ITERACIONES 80 // numero de iteraciones productor/consumidor

pthread_mutex_t mutp[NUM_PROD], mutc;  // mutexes
pthread_cond_t condp[NUM_PROD], condc; // variables de condicion
int items_totales_disponibles = 0;

// estructura de argumentos para los hilos
// pthread_create solo permite pasar un unico putero void* para cada hilo
// empaquetamos todos los argumentos necesarios en una estructura
typedef struct {
  FILE *ftexto;      // archivo de texto (para el hilo productor)
  int id;            // id del hilo
  int suma;          // suma acumulada de enteros
  int sumas_cons[3]; // para el consumidor (suma independiente de los 3
                     // ficheros)
} args_hilo;

typedef struct {
  int prioridad;              // prioridad del elemento
  int num;                    // numero leido
  long long tiempo_creacion;  // tiempo en el que se ha creado
  long long tiempo_caducidad; // tiempo que tiene para que caduque
} tipo_dato;

// estructura FIFO que se guardara en la memoria compartida
typedef struct {
  tipo_dato buffer[N];
  int tam; // tamaño de la cola
} compartido;

// variables globales compartidas entre los dos hilos
compartido comp[NUM_PROD];            // buffer compartido
long long tiempo_inicio_programa = 0; // guarda el tiempo base

// prototipos

void *hilo_productor(void *arg);
void *hilo_consumidor(void *arg);
int produce_item(FILE *f, int *suma);
void insert_item(int q, int n, int prioridad);
int remove_item(int q, int *prioridad_extraida, long long *t_creacion,
                long long *t_caducidad);
void consume_item(int item, int prioridad, long long t_creacion,
                  long long t_caducidad, int *sumas);
long long get_timestamp_ms(void);

// programa principal
int main(int argc, char **argv) {
  // capturamos el tiempo justo al iniciar el programa
  struct timeval tv;
  gettimeofday(&tv, NULL);
  tiempo_inicio_programa =
      (long long)(tv.tv_sec) * 1000 + (long long)(tv.tv_usec) / 1000;

  if (argc != 4) // comprobamos parametros
  {
    fprintf(
        stderr,
        "Uso: %s <archivo_enteros_1> <archivo_enteros_2> <archivo_enteros_3>\n",
        argv[0]);
    exit(1);
  }

  // abrimos archivos y comprobamos
  FILE *ftexto1 = fopen(argv[1], "r");
  FILE *ftexto2 = fopen(argv[2], "r");
  FILE *ftexto3 = fopen(argv[3], "r");
  if (!ftexto1 || !ftexto2 || !ftexto3) {
    perror("Error al abrir los archivos");
    exit(1);
  }

  // inicializamos el mutex y cond global del consumidor
  pthread_mutex_init(&mutc, 0);
  pthread_cond_init(&condc, 0);

  // inicializamos las 3 colas, sus mutexes y sus variables de condicion
  for (int i = 0; i < NUM_PROD; i++) {
    comp[i].tam = 0;
    memset(comp[i].buffer, 0, sizeof(comp[i].buffer));
    pthread_mutex_init(&mutp[i], 0);
    pthread_cond_init(&condp[i], 0);
  }

  // creamos los hilos
  // preparamos los argumentos para cada hilo en structs separadas
  // si usasemos la misma estructura para los dos hilos tendriamos
  // una carrera critica

  // argumentos productores
  args_hilo args_prod1 = {.ftexto = ftexto1, .id = 1, .suma = 0};
  args_hilo args_prod2 = {.ftexto = ftexto2, .id = 2, .suma = 0};
  args_hilo args_prod3 = {.ftexto = ftexto3, .id = 3, .suma = 0};

  // argumentos para consumidor
  args_hilo args_cons = {.ftexto = NULL, .id = 0, .suma = 0};

  pthread_t tid_prod1, tid_prod2, tid_prod3, tid_cons; // hilos

  // pthread_create lanza el hilo y lo pone a ejecutar la funcion indicada
  // el cuarto argumento es el void* que recibira la funcion del hilo
  if (pthread_create(&tid_prod1, NULL, hilo_productor, &args_prod1) != 0 ||
      pthread_create(&tid_prod2, NULL, hilo_productor, &args_prod2) != 0 ||
      pthread_create(&tid_prod3, NULL, hilo_productor, &args_prod3) != 0 ||
      pthread_create(&tid_cons, NULL, hilo_consumidor, &args_cons) != 0) {
    perror("Error pthread_create");
    exit(1);
  }

  // esperamos a que terminen los 3 hilos
  // pthread_join bloquea main hasta que el hilo indicado finaliza
  // sin pthread_join main podria terminar antes que los hilos matando
  // el proceso entero y dejando los hilos sin completar su trabajo
  pthread_join(tid_prod1, NULL);
  pthread_join(tid_prod2, NULL);
  pthread_join(tid_prod3, NULL);
  pthread_join(tid_cons, NULL);

  // imprimimos los resultados
  printf("[PROD 1] Suma acumulada: %d\n", args_prod1.suma);
  printf("[PROD 2] Suma acumulada: %d\n", args_prod2.suma);
  printf("[PROD 3] Suma acumulada: %d\n", args_prod3.suma);

  printf("[CONS] Sumas acumuladas: %d (PROD 1) %d (PROD 2) %d (PROD 3)\n",
         args_cons.sumas_cons[0], args_cons.sumas_cons[1],
         args_cons.sumas_cons[2]);

  // limpiamos recursos
  pthread_mutex_destroy(&mutc);
  pthread_cond_destroy(&condc);
  for (int i = 0; i < NUM_PROD; i++) {
    pthread_mutex_destroy(&mutp[i]);
    pthread_cond_destroy(&condp[i]);
  }
  fclose(ftexto1);
  fclose(ftexto2);
  fclose(ftexto3);

  exit(0);
}

// hilo productor
// funcion que ejecuta el hilo productor
// recibe un void * que casteamos a args_hilo* para acceder al archivo
// de texto y a la suma acumulada
// realiza ITERACIONES ciclos y luego activa prod_fin para avisar al consumidor
void *hilo_productor(void *arg) {
  args_hilo *a = (args_hilo *)arg;

  // semilla para los sleeps aleatorios de las ultimas iteraciones
  srand((unsigned int)time(NULL) ^ a->id);

  int q_id = a->id - 1; // indice de la cola

  for (int i = 0; i < ITERACIONES; i++) {
    // produce_item no accede al buffer, por lo que esta fuera de
    // la region critica
    int item = produce_item(a->ftexto, &a->suma);

    // si llegamos al EOF antes de completar las iteraciones rebobinamos
    if (item == -1) {
      rewind(a->ftexto);
      item = produce_item(a->ftexto, &a->suma);
    }

    // sleep fuera de la region critica para controlar la velocidad
    //  velocidad aleatoria entre 1-6s
    int t = rand() % 6 + 1;
    printf("[%lld] [PROD %d] Sleep aleatorio (Iter %d). Esperando %ds...\n",
           get_timestamp_ms(), a->id, i + 1, t);
    sleep(t);

    // region critica

    // bloqueamos exclusivamente la cola de este productor
    pthread_mutex_lock(&mutp[q_id]);

    while (comp[q_id].tam == N) // buffer lleno
    {
      pthread_cond_wait(&condp[q_id], &mutp[q_id]);
      printf("[%lld] [PROD %d] Buffer lleno en el productor (%d/%d). "
             "Esperando...\n",
             get_timestamp_ms(), a->id, comp[q_id].tam, N);
    }

    insert_item(q_id, item, a->id); // acceso al buffer
    pthread_mutex_unlock(&mutp[q_id]);
    // fin de la region critica

    // avisamos al consumidor de que hay un nuevo item global
    pthread_mutex_lock(&mutc);
    items_totales_disponibles++;
    pthread_cond_signal(&condc);
    pthread_mutex_unlock(&mutc);
  }

  printf("[%lld] [PROD %d] Terminado\n", get_timestamp_ms(), a->id);

  return NULL; // pthread_join en main recibira este valor de retorno
}

// hilo consumidor
// funcion que ejecuta el hilo consumidor
// realiza ITERACIONES ciclos extrayendo un elemento del buffer en cada uno
void *hilo_consumidor(void *arg) {
  args_hilo *a = (args_hilo *)arg;

  // semilla distinta a la del productor para los sleeps alaeatorios
  srand((unsigned int)time(NULL) ^ (unsigned int)pthread_self());

  // el consumidor debe extraer el total de items (3 productores * ITERACIONES)
  int total_items = ITERACIONES * 3;

  // ejecutamos en bucle mientras el productor no haya terminado de leer el
  // archivo o haya elementos en el buffer
  for (int i = 0; i < total_items; i++) {
    pthread_mutex_lock(&mutc);

    while (items_totales_disponibles == 0) // espera mientras este vacio
    {
      printf("[%lld] [CONS] Todos los buffers vacios. Esperando...\n",
             get_timestamp_ms());
      pthread_cond_wait(&condc, &mutc);
    }

    items_totales_disponibles--;
    pthread_mutex_unlock(&mutc); // desbloqueamos el mutex global

    // region critica
    int prioridad_extraida = 0;
    int item = 0;
    long long t_creacion = 0;
    long long t_caducidad = 0;
    int extraido = 0;

    while (!extraido) {
      // trylock: si el productor tiene tiene bloqueada la cola, no nos
      // atascamos, pasamos instantaneamente a comprobar la siguiente prioridad
      for (int q = 0; q < NUM_PROD; q++) {
        if (pthread_mutex_trylock(&mutp[q]) == 0) {
          // si el buffer no esta vacio
          if (comp[q].tam > 0) {
            item =
                remove_item(q, &prioridad_extraida, &t_creacion, &t_caducidad);
            extraido = 1;
            pthread_cond_signal(
                &condp[q]); // despertamos el productor de esta cola especifica
          }
          pthread_mutex_unlock(&mutp[q]);

          if (extraido)
            break; // si ya extrajimos, salimos del bucle for
        }
        // si el bucle termina y no ha extraido nada (porque justo en ese
        // milisegundo los productores tenian los mutexes bloqueados
        // insertando), el while vuelve a intentar inmediatamente
      }
    }
    // procesamos el item
    consume_item(item, prioridad_extraida, t_creacion, t_caducidad,
                 a->sumas_cons);
  }

  printf("[%lld] [CONS] Terminado\n", get_timestamp_ms());
  return NULL;
}

// funcion que lee un unico entero del archivo y actualiza la suma acumulada
// del productor
// fscanf ignora automaticamente los espacios en blanco, saltos de línea o
// tabulaciones devuelve -1 al llegar al EOF no accede al buffer compartido
int produce_item(FILE *f, int *suma) {
  int numero_leido;

  // fscanf intenta leer un entero. Si devuelve 1, la lectura fue exitosa.
  if (fscanf(f, "%d", &numero_leido) == 1) {
    *suma += numero_leido;
    return numero_leido;
  }

  // si se llega al final del archivo se devuelve -1
  return -1;
}

// funcion que inserta el entero en el final de estructura FIFO
// siempre se llama dentro de la region critica por lo que el acceso
// a comp es seguro
void insert_item(int q, int n, int prioridad) {
  long long creacion = get_timestamp_ms();
  long long caducidad = (rand() % 12 + 1) * 1000;

  comp[q].buffer[comp[q].tam].num = n;
  comp[q].buffer[comp[q].tam].prioridad = prioridad;
  comp[q].buffer[comp[q].tam].tiempo_creacion = creacion;
  comp[q].buffer[comp[q].tam].tiempo_caducidad = caducidad;
  comp[q].tam++;

  printf("[%lld] [PROD %d] Insertado '%d'. Longitud: %d | Caducidad: %llds\n",
         creacion, prioridad, n, comp[q].tam, caducidad / 1000);
}

// funcion que retira el elemento del principio de la cola FIFO y lo
// reemplaza por 0
// siempre se llama dentro de la region critica igual que insert_item
int remove_item(int q, int *prioridad_extraida, long long *t_creacion,
                long long *t_caducidad) {
  // con 3 colas es FIFO puro, cogemos siempre el de la posicion 0
  int n = comp[q].buffer[0].num;
  *prioridad_extraida = comp[q].buffer[0].prioridad;
  *t_creacion = comp[q].buffer[0].tiempo_creacion;
  *t_caducidad = comp[q].buffer[0].tiempo_caducidad;

  // desplazamos a la izquierda
  for (int i = 0; i < comp[q].tam - 1; i++) {
    comp[q].buffer[i] = comp[q].buffer[i + 1];
  }

  comp[q].tam--;
  comp[q].buffer[comp[q].tam].num = 0;
  comp[q].buffer[comp[q].tam].prioridad = 0;

  printf("[%lld] [CONS] Extraido '%d' (Prioridad %d). Longitud: %d\n",
         get_timestamp_ms(), n, *prioridad_extraida, comp[q].tam);

  return n;
}

// funcion que actualiza el suma acumulada del consumidor con el
// item retirado
// no accede al buffer compartido
void consume_item(int item, int prioridad, long long t_creacion,
                  long long t_caducidad, int *sumas) {
  long long tiempo_actual = get_timestamp_ms();
  long long tiempo_transcurrido = tiempo_actual - t_creacion;

  // si ha pasado mas tiempo que la caducidad esta caducado
  if (tiempo_transcurrido <= t_caducidad) {
    // sleep para controlar la velocidad
    // velocidades aleatorias entre 1-3s
    int t = rand() % 3 + 1;
    printf("[%lld] [CONS] Procesando '%d' (Prioridad %d). Esperando %ds...\n",
           get_timestamp_ms(), item, prioridad, t);
    sleep(t);

    // sumamos el entero recibido al acumulador del consumidor
    sumas[prioridad - 1] += item; // suma especifica del fichero
  } else
    printf("[%lld] [CONS] Item '%d' (Prioridad %d) caducado. No se suma. "
           "Tiempo transcurrido: %llds\n",
           get_timestamp_ms(), item, prioridad, tiempo_transcurrido / 1000);

  // imprimimos el valor procesado para seguimiento
  printf("[%lld] [CONS] Consumido '%d' (Prioridad %d). Sumas acumuladas: %d "
         "(PROD 1) %d (PROD 2) %d (PROD 3)\n",
         get_timestamp_ms(), item, prioridad, sumas[0], sumas[1], sumas[2]);
}

// funcion auxiliar para timestamps
long long get_timestamp_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  long long tiempo_actual =
      (long long)(tv.tv_sec) * 1000 + (long long)(tv.tv_usec) / 1000;

  return tiempo_actual - tiempo_inicio_programa;
}