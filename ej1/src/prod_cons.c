#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>   // hilos
#include <semaphore.h> // semaforos
#include <time.h>

// productor y consumidor son dos hilos dentro del mismo proceso
// al compartir el espacio de direcciones la estructura compartida
// es una variable global (no se necesita un archivo de memoria
// compartida para mapear en memoria)

#define N 10 // tamaño del buffer

// estructura de argumentos para los hilos
// pthread_create solo permite pasar un unico putero void* para cada hilo
// empaquetamos todos los argumentos necesarios en una estructura
typedef struct
{
    FILE *ftexto; // archivo de texto (para el hilo productor)
    int suma;     // suma acumulada de enteros
} args_hilo;

// estructura FIFO que se guardara en la memoria compartida
typedef struct
{
    int buffer[N];
    int inicio;   // indice inicio de la cola
    int fin;      // indice final de la cola
    int prod_fin; // bandera para avisar al consumidor que el productor termino
} compartido;

// variables globales compartidas entre los dos hilos
compartido comp; // buffer compartido

// prototipos

void *hilo_productor(void *arg);
void *hilo_consumidor(void *arg);
int produce_item(FILE *f, int *suma);
void insert_item(int n);
int remove_item(void);
void consume_item(int item, int *suma_consumida);
void imprimir_buffer(void);

// programa principal
int main(int argc, char **argv)
{
    if (argc != 2) // comprobamos parametros
    {
        fprintf(stderr, "Uso: %s <archivo_enteros>\n", argv[0]);
        exit(1);
    }

    // abrimos archivo y comprobamos
    FILE *ftexto = fopen(argv[1], "r");
    if (!ftexto)
    {
        perror("Error al abrir el archivo de texto");
        exit(1);
    }

    // inicializamos la estructura compartida
    comp.inicio = 0;
    comp.fin = 0;
    comp.prod_fin = 0;
    memset(comp.buffer, 0, sizeof(comp.buffer));

    // creamos los hilos
    // preparamos los argumentos para cada hilo en structs separadas
    // si usasemos la misma estructura para los dos hilos tendriamos
    // una carrera critica sobre count_vocales
    args_hilo args_prod = {.ftexto = ftexto};
    args_hilo args_cons = {.ftexto = NULL};
    memset(&args_prod.suma, 0, sizeof(args_prod.suma));
    memset(&args_cons.suma, 0, sizeof(args_cons.suma));

    pthread_t tid_prod, tid_cons; // ids

    // pthread_create lanza el hilo y lo pone a ejecutar la funcion indicada
    // el cuarto argumento es el void* que recibira la funcion del hilo
    if (
        pthread_create(&tid_prod, NULL, hilo_productor, &args_prod) != 0 ||
        pthread_create(&tid_cons, NULL, hilo_consumidor, &args_cons) != 0)
    {
        perror("Error pthread_create");
        exit(1);
    }

    // esperamos a que terminen los dos hilos
    // pthread_join bloquea main hasta que el hilo indicado finaliza
    // sin pthread_join main podria terminar antes que los hilos matando
    // el proceso entero y dejando los hilos sin completar su trabajo
    pthread_join(tid_prod, NULL);
    pthread_join(tid_cons, NULL);

    // imprimimos los resultados
    printf("[PROD] Suma acumulada: %d\n", args_prod.suma);

    printf("[CONS] Suma acumulada: %d\n", args_cons.suma);

    // limpiamos recursos
    fclose(ftexto);

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

    while (1)
    {
        // produce_item no accede al buffer, por lo que esta fuera de
        // la region critica
        int item = produce_item(a->ftexto, &a->suma);

        // si item es -1 significa que hemos llegado al fin del archivo por
        // lo tanto salimos del bucle
        if (item == -1) // EOF
            break;

        while ((comp.fin + 1) % N == comp.inicio) // realizamos espera activa
        {
            printf("[PROD] Buffer lleno en el productor (%d/%d). Esperando...\n", comp.fin, N);
            sleep(1);
        }

        // region critica

        insert_item(item); // acceso exclusivo al buffer

        // fin de la region critica
    }

    // avisamos al consumidor de que no habra mas datos
    // el consumidor solo la lee cuando llenas == 0
    // es decir cuando ya no queda ningun elemento en el buffer
    // por lo que no puede haber conflicto con insert_item
    comp.prod_fin = 1;
    printf("[PROD] Terminado. Avisando al consumidor...\n");

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
    while (!comp.prod_fin || comp.inicio != comp.fin)
    {
        while (comp.inicio == comp.fin) // espera activa
        {
            // si el productor termino y el buffer esta vacio, salimos del hilo
            if (comp.prod_fin)
            {
                printf("[CONS] Terminado...\n");
                return NULL;
            }

            printf("[CONS] Buffer vacio. Esperando...\n");
            sleep(1);
        }

        // region critica
        int item = remove_item();
        // fin de la region critica

        // procesamos el item fuera de la region critica
        consume_item(item, &a->suma);
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
void insert_item(int n)
{
    // inicio de la region critica

    // almacenamos temporalmente el valor actual de top
    int fin_actual = comp.fin; // leeemos el indice actual del final

    // calculamos el indice en aritmetica modular para que no se salga del buffer
    comp.fin = (fin_actual + 1) % N;

    // forzamos que el proceso se duerma para aumentar
    // la probabilidad de carrera critica
    // si el consumidor ejecuta remove_item()
    // decrementara comp->fin
    // cuando el productor retome, escribira en comp->buffer[comp->fin]
    // (diferente al top_actual) y luego fijara fin = fin_actual + 1
    // corrompiendo el indice y sobreescribiendo una posicion incorrecta
    sleep(1); // forzamos carrera

    comp.buffer[fin_actual] = n;

    // fin de la region critica
    printf("[PROD] Insertado '%d'. Inicio: %d | Fin: %d\n", n, comp.inicio, comp.fin);
    imprimir_buffer();
}

// funcion que retira el elemento del principio de la cola FIFO y lo
// reemplaza por 0
// siempre se llama dentro de la region critica igual que insert_item
int remove_item(void)
{
    int n;

    // inicio de la región critica

    int inicio_actual = comp.inicio; // guardamos el indice actual

    // realizamos aritmetica modular para que el indice no se salga del buffer
    comp.inicio = (inicio_actual + 1) % N;

    // forzamos que el proceso se duerma para aumentar
    // la probabilidad de una carrera critica
    // si el prodictor inserta un nuevo elemnto incrementara
    // comp.fin
    // cuando el consumidor retome la ejecucion fijara
    // comp.fin = fin_actual borrando efectivamente el elemento
    // recien insertado (perdiendo el dato)
    sleep(0.5);

    n = comp.buffer[inicio_actual]; // leemos el elemento

    comp.buffer[inicio_actual] = 0; // sustituimos el entero por 0

    // fin de la region critica
    printf("[CONS] Eliminado '%d'. Inicio: %d | Fin: %d\n", n, comp.inicio, comp.fin);
    imprimir_buffer();

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

void imprimir_buffer(void)
{
    printf("Buffer: [ ");
    for (int i = 0; i < N; i++)
        printf("%2d ", comp.buffer[i]);
    printf("]\n");
}