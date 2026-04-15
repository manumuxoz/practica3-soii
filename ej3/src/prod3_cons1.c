#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>   // hilos
#include <time.h>

// productor y consumidor son dos hilos dentro del mismo proceso
// al compartir el espacio de direcciones la estructura compartida
// es una variable global (no se necesita un archivo de memoria
// compartida para mapear en memoria)

#define N 10 // tamaño del buffer

#define ITERACIONES 80 // numero de iteraciones productor/consumidor

pthread_mutex_t mut; // mutex
pthread_cond_t condc, condp; // variables de condicion

// estructura de argumentos para los hilos
// pthread_create solo permite pasar un unico putero void* para cada hilo
// empaquetamos todos los argumentos necesarios en una estructura
typedef struct
{
    FILE *ftexto; // archivo de texto (para el hilo productor)
    int id; // id del hilo
    int suma; // suma acumulada de enteros
} args_hilo;

typedef struct
{
    int prioridad; // prioridad del elemento
    int num; // numero leido
} tipo_dato;

// estructura FIFO que se guardara en la memoria compartida
typedef struct
{
    tipo_dato buffer[N];
    int inicio; // indice inicio de la cola
    int fin;    // indice final de la cola
    int tam;    // tamaño de la cola
} compartido;

// variables globales compartidas entre los dos hilos
compartido comp; // buffer compartido

// prototipos

void *hilo_productor(void *arg);
void *hilo_consumidor(void *arg);
int produce_item(FILE *f, int *suma);
void insert_item(int n, int prioridad);
int remove_item(void);
void consume_item(int item, int *suma_consumida);

// programa principal
int main(int argc, char **argv)
{
    if (argc != 4) // comprobamos parametros
    {
        fprintf(stderr, "Uso: %s <archivo_enteros_1> <archivo_enteros_2> <archivo_enteros_3>\n", argv[0]);
        exit(1);
    }

    // abrimos archivos y comprobamos
    FILE *ftexto1 = fopen(argv[1], "r");
    FILE *ftexto2 = fopen(argv[2], "r");
    FILE *ftexto3 = fopen(argv[3], "r");
    if (!ftexto1 || !ftexto2 || !ftexto3)
    {
        perror("Error al abrir el archivo de texto");
        exit(1);
    }

    // inicializamos las estructuras compartidas
    comp.inicio = 0; comp.fin = 0; comp.tam = 0;
    memset(comp.buffer, 0, sizeof(comp.buffer));

    // inicializamos el mutex y las variables de condicion
    pthread_mutex_init(&mut, 0);
    pthread_cond_init(&condc, 0);
    pthread_cond_init(&condp, 0);

    // creamos los hilos
    // preparamos los argumentos para cada hilo en structs separadas
    // si usasemos la misma estructura para los dos hilos tendriamos
    // una carrera critica sobre count_vocales
    args_hilo args_prod1 = {.ftexto = ftexto1};
    args_hilo args_prod2 = {.ftexto = ftexto2};
    args_hilo args_prod3 = {.ftexto = ftexto3};
    args_hilo args_cons = {.ftexto = NULL};
    memset(&args_prod1.suma, 0, sizeof(args_prod1.suma));
    memset(&args_prod2.suma, 0, sizeof(args_prod2.suma));
    memset(&args_prod3.suma, 0, sizeof(args_prod3.suma));
    memset(&args_cons.suma, 0, sizeof(args_cons.suma));
    memset(&args_prod1.id, 1, sizeof(args_prod1.id));
    memset(&args_prod2.id, 2, sizeof(args_prod2.id));
    memset(&args_prod3.id, 3, sizeof(args_prod3.id));
    memset(&args_cons.id, 0, sizeof(args_cons.id));
    

    pthread_t tid_prod1, tid_prod2, tid_prod3, tid_cons; // ids

    // pthread_create lanza el hilo y lo pone a ejecutar la funcion indicada
    // el cuarto argumento es el void* que recibira la funcion del hilo
    if (
        pthread_create(&tid_prod1, NULL, hilo_productor, &args_prod1) != 0 ||
        pthread_create(&tid_prod2, NULL, hilo_productor, &args_prod2) != 0 ||
        pthread_create(&tid_prod3, NULL, hilo_productor, &args_prod3) != 0 ||
        pthread_create(&tid_cons, NULL, hilo_consumidor, &args_cons) != 0)
    {
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

    printf("[CONS] Suma acumulada: %d\n", args_cons.suma);

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
void *hilo_productor(void *arg)
{
    args_hilo *a = (args_hilo *)arg;

    // semilla para los sleeps aleatorios de las ultimas iteraciones
    srand((unsigned int)time(NULL));

    for (int i = 0; i < ITERACIONES; i++)
    {
        // produce_item no accede al buffer, por lo que esta fuera de
        // la region critica
        int item = produce_item(a->ftexto, &a->suma);

        // si llegamos al EOF antes de completar las iteraciones rebobinamos
        if (item == -1)
        {
            rewind(a->ftexto);
            item = produce_item(a->ftexto, &a->suma);
        }

        // sleep fuera de la region critica para controlar la velocidad
        //  velocidad aleatoria entre 1-6s 
        int t = rand() % 7 + 1;
        printf("[PROD] Sleep aleatorio (Iter %d). Esperando %ds...\n", i + 1, t);
        sleep(t);

        // region critica
        pthread_mutex_lock(&mut);

        while(comp.tam == N) // buffer lleno
        {
            pthread_cond_wait(&condp, &mut);
            printf("[PROD] Buffer lleno en el productor (%d/%d). Esperando...\n", comp.tam, N);
        }
    
        insert_item(item, a->id); // acceso al buffer
        // fin de la region critica

        pthread_cond_signal(&condc); // despierta al consumidor
        pthread_mutex_unlock(&mut);
    }

    printf("[PROD] Terminado\n");

    return NULL; // pthread_join en main recibira este valor de retorno
}

// hilo consumidor
// funcion que ejecuta el hilo consumidor
// realiza ITERACIONES ciclos extrayendo un elemento del buffer en cada uno
void *hilo_consumidor(void *arg)
{
    args_hilo *a = (args_hilo *)arg;

    // semilla distinta a la del productor para los sleeps alaeatorios
    srand((unsigned int)time(NULL) ^ (unsigned int)pthread_self());

    // ejecutamos en bucle mientras el productor no haya terminado de leer el archivo
    // o haya elementos en el buffer
    for (int i = 0; i < ITERACIONES; i++)
    {
        pthread_mutex_lock(&mut);
    
        while (comp.tam == 0) // espera mientras este vacio
        {
            pthread_cond_wait(&condc, &mut);
            printf("[CONS] Buffer vacio. Esperando...\n");
        }

        // region critica
        int item = remove_item();

        pthread_cond_signal(&condp); // despierta al productor
        pthread_mutex_unlock(&mut);
        // fin de la region critica

        // procesamos el item fuera de la region critica
        consume_item(item, &a->suma);

        // sleep fuera de la region critica para controlar la velocidad
        // velocidades aleatorias entre 1-3s
        int t = rand() % 4 + 1;
        printf("[CONS] Sleep aleatorio (Iter %d). Esperando %ds...\n", i + 1, t);
        sleep(t);
    }

    printf("[CONS] Terminado\n");
    return NULL;
}

// funcion que lee un unico entero del archivo y actualiza la suma acumulada
// del productor
// fscanf ignora automaticamente los espacios en blanco, saltos de línea o tabulaciones
// devuelve -1 al llegar al EOF
// no accede al buffer compartido
int produce_item(FILE *f, int *suma)
{
    int numero_leido;

    // fscanf intenta leer un entero. Si devuelve 1, la lectura fue exitosa.
    if (fscanf(f, "%d", &numero_leido) == 1)
    {
        *suma += numero_leido;

        // Imprimimos el valor procesado para seguimiento
        printf("Producido: %d | Suma acumulada: %d\n", numero_leido, *suma);

        return numero_leido;
    }

    // si se llega al final del archivo se devuelve -1
    return -1;
}

// funcion que inserta el entero en el final de estructura FIFO
// siempre se llama dentro de la region critica por lo que el acceso
// a comp es seguro
void insert_item(int n, int prioridad)
{
    comp.buffer[comp.fin].num = n;
    comp.buffer[comp.fin].prioridad = prioridad;

    // calculamos el indice en aritmetica modular para que no se salga del buffer
    comp.fin = (comp.fin + 1) % N;

    comp.tam++; // aumentamos tamaño de la cola

    printf("[PROD] Insertado '%d' (Prioridad %d). Inicio: %d | Fin: %d | Longitud: %d\n", n, prioridad, comp.inicio, comp.fin, comp.tam);
}

// funcion que retira el elemento del principio de la cola FIFO y lo
// reemplaza por 0
// siempre se llama dentro de la region critica igual que insert_item
int remove_item(void)
{
    int n = 0;

    for (int i = comp.inicio; i < comp.fin; i= ((i + 1) % comp.tam))
    {
        int prioridad = comp.buffer[i].prioridad;

        if (prioridad == 1)
        {
            n = comp.buffer[i].num; // leemos el elemento
    
            comp.buffer[i].num = 0; // sustituimos el entero por 0
            comp.buffer[i].prioridad = 0;
    
            // realizamos aritmetica modular para que el indice no se salga del buffer
            comp.inicio = (comp.inicio + 1) % N;

            comp.tam--; // reducimos tamaño de la cola

            // fin de la region critica
            printf("[CONS] Eliminado '%d' (Prioridad %d). Inicio: %d | Fin: %d | Longitud: %d\n", n, prioridad, comp.inicio, comp.fin, comp.tam);

            return n;
        }
    }

    for (int i = comp.inicio; i < comp.fin; i = ((i + 1) % comp.tam))
    {
        int prioridad = comp.buffer[i].prioridad;

        if (prioridad == 2)
        {
            n = comp.buffer[i].num; // leemos el elemento
    
            comp.buffer[i].num = 0; // sustituimos el entero por 0
            comp.buffer[i].prioridad = 0;
    
            // realizamos aritmetica modular para que el indice no se salga del buffer
            comp.inicio = (comp.inicio + 1) % N;

            comp.tam--; // reducimos tamaño de la cola

            // fin de la region critica
            printf("[CONS] Eliminado '%d' (Prioridad %d). Inicio: %d | Fin: %d | Longitud: %d\n", n, prioridad, comp.inicio, comp.fin, comp.tam);

            return n;
        }
    }

    for (int i = comp.inicio; i < comp.fin; i = ((i + 1) % comp.tam))
    {
        int prioridad = comp.buffer[i].prioridad;

        if (prioridad == 3)
        {
            n = comp.buffer[i].num; // leemos el elemento
    
            comp.buffer[i].num = 0; // sustituimos el entero por 0
            comp.buffer[i].prioridad = 0;
    
            // realizamos aritmetica modular para que el indice no se salga del buffer
            comp.inicio = (comp.inicio + 1) % N;

            comp.tam--; // reducimos tamaño de la cola

            // fin de la region critica
            printf("[CONS] Eliminado '%d' (Prioridad %d). Inicio: %d | Fin: %d | Longitud: %d\n", n, prioridad, comp.inicio, comp.fin, comp.tam);

            return n;
        }
    }

    return n;
}

// funcion que actualiza el suma acumulada del consumidor con el
// item retirado
// no accede al buffer compartido
void consume_item(int item, int *suma_consumida)
{
    // Sumamos el entero recibido al acumulador del consumidor
    *suma_consumida += item;

    // Imprimimos el valor procesado para seguimiento
    printf("Consumido: %d | Suma acumulada: %d\n", item, *suma_consumida);
}