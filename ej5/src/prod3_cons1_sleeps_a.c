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
// la funcion consume_item() tarda 3 segundos en procesar un item de prioridad 1
// 2 segundos en procesar un item de prioridad 2
// 1 segundo en procesar un item de prioridad 3

#define N 10 // tamaño del buffer

#define ITERACIONES 80 // numero de iteraciones productor/consumidor

pthread_mutex_t mut;         // mutex
pthread_cond_t condc, condp; // variables de condicion

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
  long long tiempo_creacion;  // tiempo de creacion
  long long tiempo_caducidad; // tiempo de caducidad
} tipo_dato;

// estructura FIFO que se guardara en la memoria compartida
typedef struct {
  tipo_dato buffer[N];
  int tam; // tamaño de la cola
} compartido;

// variables globales compartidas entre los dos hilos
compartido comp;                      // buffer compartido
long long tiempo_inicio_programa = 0; // guarda el tiempo base

// prototipos

void *hilo_productor(void *arg);
void *hilo_consumidor(void *arg);
int produce_item(FILE *f, int *suma);
void insert_item(int n, int prioridad);
int remove_item(int *prioridad_extraida, long long *tiempo_creacion_extraido,
                long long *tiempo_caducidad_extraido);
void consume_item(int item, int prioridad, long long tiempo_creacion_extraido,
                  long long tiempo_caducidad_extraido, int *sumas);
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

  // inicializamos las estructuras compartidas
  comp.tam = 0;
  memset(comp.buffer, 0, sizeof(comp.buffer));

  // inicializamos el mutex y las variables de condicion
  pthread_mutex_init(&mut, 0);
  pthread_cond_init(&condc, 0);
  pthread_cond_init(&condp, 0);

  // creamos los hilos
  // preparamos los argumentos para cada hilo en structs separadas
  // si usasemos la misma estructura para los dos hilos tendriamos
  // una carrera critica sobre count_vocales

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
  pthread_mutex_destroy(&mut);
  pthread_cond_destroy(&condc);
  pthread_cond_destroy(&condp);
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
    pthread_mutex_lock(&mut);

    while (comp.tam == N) // buffer lleno
    {
      pthread_cond_wait(&condp, &mut);
      printf("[%lld] [PROD %d] Buffer lleno en el productor (%d/%d). "
             "Esperando...\n",
             get_timestamp_ms(), a->id, comp.tam, N);
    }

    insert_item(item, a->id); // acceso al buffer
    // fin de la region critica

    pthread_cond_signal(&condc); // despierta al consumidor
    pthread_mutex_unlock(&mut);
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
    pthread_mutex_lock(&mut);

    while (comp.tam == 0) // espera mientras este vacio
    {
      pthread_cond_wait(&condc, &mut);
      printf("[%lld] [CONS] Buffer vacio. Esperando...\n", get_timestamp_ms());
    }

    // region critica
    int prioridad_extraida;
    long long tiempo_creacion_extraido, tiempo_caducidad_extraido;
    int item = remove_item(&prioridad_extraida, &tiempo_creacion_extraido,
                           &tiempo_caducidad_extraido);

    // despertamos a todos los productores (broadcast) que puedan estar
    // bloqueados
    pthread_cond_broadcast(&condp);
    pthread_mutex_unlock(&mut);
    // fin de la region critica

    // procesamos el item fuera de la region critica
    consume_item(item, prioridad_extraida, tiempo_creacion_extraido,
                 tiempo_caducidad_extraido, a->sumas_cons);
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
void insert_item(int n, int prioridad) {
  long long creacion = get_timestamp_ms();
  long long caducidad = (rand() % 12 + 1) * 1000;

  // insertamos al final del array actual
  comp.buffer[comp.tam].num = n;
  comp.buffer[comp.tam].prioridad = prioridad;
  comp.buffer[comp.tam].tiempo_creacion = creacion;
  comp.buffer[comp.tam].tiempo_caducidad = caducidad;
  comp.tam++; // aumentamos tamaño de la cola

  printf("[%lld] [PROD %d] Insertado '%d' (Prioridad %d). Longitud: %d | "
         "Tiempo de caducidad: %llds\n",
         get_timestamp_ms(), prioridad, n, prioridad, comp.tam,
         caducidad / 1000);
}

// funcion que retira el elemento del principio de la cola FIFO y lo
// reemplaza por 0
// siempre se llama dentro de la region critica igual que insert_item
int remove_item(int *prioridad_extraida, long long *tiempo_creacion_extraido,
                long long *tiempo_caducidad_extraido) {
  int mejor_idx = 0;

  // buscamos el elemento con mayor prioridad (numero mas bajo)
  for (int i = 0; i < comp.tam; i++) {
    if (comp.buffer[i].prioridad ==
        1) // si el item tiene prioridad 1 salimos del bucle directamente
    {
      mejor_idx = i;
      break;
    }

    if (comp.buffer[i].prioridad < comp.buffer[mejor_idx].prioridad)
      mejor_idx = i;
  }

  int n = comp.buffer[mejor_idx].num;
  *prioridad_extraida = comp.buffer[mejor_idx].prioridad;
  *tiempo_creacion_extraido = comp.buffer[mejor_idx].tiempo_creacion;
  *tiempo_caducidad_extraido = comp.buffer[mejor_idx].tiempo_caducidad;

  // desplazamos los elementos a la izquierda para eliminar el hueco
  // mantenemos el orden de los elementos con la misma prioridad
  for (int i = mejor_idx; i < comp.tam - 1; i++)
    comp.buffer[i] = comp.buffer[i + 1];

  comp.tam--;                    // reducimos tamaño de la cola
  comp.buffer[comp.tam].num = 0; // limpiamos por seguridad
  comp.buffer[comp.tam].prioridad = 0;
  // fin de la region critica

  printf("[%lld] [CONS] Eliminado '%d' (Prioridad %d). Longitud: %d\n",
         get_timestamp_ms(), n, *prioridad_extraida, comp.tam);

  return n;
}

// funcion que actualiza el suma acumulada del consumidor con el
// item retirado
// no accede al buffer compartido
void consume_item(int item, int prioridad, long long tiempo_creacion_extraido,
                  long long tiempo_caducidad_extraido, int *sumas) {
  long long tiempo_actual = get_timestamp_ms();
  long long tiempo_transcurrido = tiempo_actual - tiempo_creacion_extraido;

  // si ha pasado mas tiempo que la caducidad esta caducado
  if (tiempo_transcurrido <= tiempo_caducidad_extraido) {
    // dormimos el consumidor dependiendo de la prioridad del item
    if (prioridad == 1)
      sleep(3);
    else if (prioridad == 2)
      sleep(2);
    else
      sleep(1);

    printf("[%lld] [CONS] Procesando '%d' (Prioridad %d). Esperando %ds...\n",
           get_timestamp_ms(), item, prioridad,
           prioridad == 1   ? 3
           : prioridad == 2 ? 2
                            : 1);

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