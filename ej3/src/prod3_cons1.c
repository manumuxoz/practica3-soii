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
    int suma;     // suma acumulada de enteros
} args_hilo;

// estructura FIFO que se guardara en la memoria compartida
typedef struct
{
    int buffer[N];
    int inicio; // indice inicio de la cola
    int fin;    // indice final de la cola
    int tam;    // tamaño de la cola
} compartido;

// variables globales compartidas entre los dos hilos
// creamos tres bufferes para la prioridad
// comp1 (prioridad 1), comp2 (prioridad 2), comp3 (prioridad 3)
compartido comp1, comp2, comp3; // buffers compartidos

// prototipos

void *hilo_productor(void *arg);
void *hilo_consumidor(void *arg);
int produce_item(FILE *f, int *suma);
void insert_item(int n);
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
    comp1.inicio = 0; comp1.fin = 0; comp1.tam = 0;
    comp2.inicio = 0; comp2.fin = 0; comp2.tam = 0;
    comp3.inicio = 0; comp3.fin = 0; comp3.tam = 0;
    memset(comp1.buffer, 0, sizeof(comp1.buffer));
    memset(comp2.buffer, 0, sizeof(comp2.buffer));
    memset(comp3.buffer, 0, sizeof(comp3.buffer));

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
        // iteraciones 0-29 el buffer tiene que llenarse (productor rapido)
        // iteraciones 30-59 el consumidor vacia el buffer (productor lento)
        // iteraciones 60-79 velocidad aleatoria entre 0-3s 
        if (i < 30)
            sleep(0);
        else if (i < 60)
        {
            printf("[PROD] Voy lento (Iter %d). Esperando 2s...\n", i + 1);
            sleep(2);
        }
        else
        {
            int t = rand() % 4;
            printf("[PROD] Sleep aleatorio (Iter %d). Esperando %ds...\n", i + 1, t);
            sleep(t);
        }

        // region critica
        pthread_mutex_lock(&mut);

        while(comp.tam == N) // buffer lleno
        {
            pthread_cond_wait(&condp, &mut);
            printf("[PROD] Buffer lleno en el productor (%d/%d). Esperando...\n", comp.tam, N);
        }
    
        insert_item(item); // acceso al buffer
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
        // iteraciones 0-29 el buffer tiene que llenarse (consumidor lento)
        // iteracions 30-59 el buffer tiene que vaciarse (consumidor rapido)
        // iteraciones 60-79 velocidades aleatorias entre 0-3s
        if (i < 30)
        {
            printf("[CONS] Voy lento (Iter %d). Esperando 2s...\n", i + 1);
            sleep(2);
        }
        else if (i < 60)
            sleep(0);
        else
        {
            int t = rand() % 4;
            printf("[CONS] Sleep aleatorio (Iter %d). Esperando %ds...\n", i + 1, t);
            sleep(t);
        }
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
    comp.buffer[comp.fin] = n;

    // calculamos el indice en aritmetica modular para que no se salga del buffer
    comp.fin = (comp.fin + 1) % N;

    comp.tam++; // aumentamos tamaño de la cola

    printf("[PROD] Insertado '%d'. Inicio: %d | Fin: %d | Longitud: %d\n", n, comp.inicio, comp.fin, comp.tam);
}

// funcion que retira el elemento del principio de la cola FIFO y lo
// reemplaza por 0
// siempre se llama dentro de la region critica igual que insert_item
int remove_item(void)
{
    int n = comp.buffer[comp.inicio]; // leemos el elemento
    
    comp.buffer[comp.inicio] = 0; // sustituimos el entero por 0
    
    // realizamos aritmetica modular para que el indice no se salga del buffer
    comp.inicio = (comp.inicio + 1) % N;

    comp.tam--; // reducimos tamaño de la cola

    // fin de la region critica
    printf("[CONS] Eliminado '%d'. Inicio: %d | Fin: %d | Longitud: %d\n", n, comp.inicio, comp.fin, comp.tam);

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