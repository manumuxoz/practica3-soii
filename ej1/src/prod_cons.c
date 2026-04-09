#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>    // hilos
#include <semaphore.h>  // semaforos
#include <time.h>
#include "compartido.h"

// productor y consumidor son dos hilos dentro del mismo proceso
// al compartir el espacio de direcciones la estructura compartida
// es una variable global (no se necesita un archivo de memoria
// compartida para mapear en memoria)
// emplearemos semaforos anonimos POSIX (sem_init/sem_destroy) ya que
// no es necesario idetificarlos en el kernel (seran variables globales)

// estructura de argumentos para los hilos
// pthread_create solo permite pasar un unico putero void* para cada hilo
// empaquetamos todos los argumentos necesarios en una estructura
typedef struct
{
    FILE *ftexto; // archivo de texto (para el hilo productor)
    int count_vocales[5]; // contador de vocales
} args_hilo;

// variables globales compartidas entre los dos hilos
compartido comp; // buffer compartido

// semaforos anonimos
sem_t mutex; // garantiza exclusion mutua sobre el buffer
sem_t vacias; // cuenta posiciones libres
sem_t llenas; // cuenta posiciones ocupadas

// prototipos

void *hilo_productor(void *arg);
void *hilo_consumidor(void *arg);
char produce_item(FILE *f, int count_vocales[5]);
void insert_item(char c);
char remove_item(void);
void consume_item(char c, int count_vocales[5]);

int main(int argc, char **argv)
{
    if (argc != 2) // comprobamos parametros
    {
        fprintf(stderr, "Uso: %s <archivo_texto>\n", argv[0]);
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
    comp.top = 0;
    comp.prod_fin = 0;
    memset(comp.buffer, 0, sizeof(comp.buffer));

    // inicializamos los semaforos anonimos:
    // sem_init(sem, pshared, valor)
    // pshared 0 el semaforo es local al proceso(compartido entre hilos)
    // pshared 1 compartido entre procesos mediante memoria compartida
    // (caso sem_open)
    // usamos pshared 0 porque aqui los consumidores del semaforo son
    // hilos del mismo proceso, no procesos distintos
    if (
        sem_init(&mutex, 0, 1) || // region critica libre al inicio
        sem_init(&vacias, 0, N) || // buffer vacio (N huecos libres)
        sem_init(&llenas, 0, 0) // ningun elemento producido todavia
    )
    {
        perror("Error sem_init");
        exit(1);
    }

    // creamos los hilos
    // preparamos los argumentos para cada hilo en structs separadas
    // si usasemos la misma estructura para los dos hilos tendriamos
    // una carrera critica sobre count_vocales
    args_hilo args_prod = { .ftexto = ftexto };
    args_hilo args_cons = { .ftexto = NULL };
    memset(args_prod.count_vocales, 0, sizeof(args_prod.count_vocales));
    memset(args_cons.count_vocales, 0, sizeof(args_cons.count_vocales));
    
    pthread_t tid_prod, tid_cons; //ids

    // pthread_create lanza el hilo y lo pone a ejecutar la funcion indicada
    // el cuarto argumento es el void* que recibira la funcion del hilo
    if (
        pthread_create(&tid_prod, NULL, hilo_productor, &args_prod) != 0 ||
        pthread_create(&tid_cons, NULL, hilo_consumidor, &args_cons) != 0
    )
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
    printf("[PROD] Vocales leidas del archivo:\n");
    printf("  a: %d\n", args_prod.count_vocales[0]);
    printf("  e: %d\n", args_prod.count_vocales[1]);
    printf("  i: %d\n", args_prod.count_vocales[2]);
    printf("  o: %d\n", args_prod.count_vocales[3]);
    printf("  u: %d\n", args_prod.count_vocales[4]);
 
    printf("[CONS] Vocales consumidas del buffer:\n");
    printf("  a: %d\n", args_cons.count_vocales[0]);
    printf("  e: %d\n", args_cons.count_vocales[1]);
    printf("  i: %d\n", args_cons.count_vocales[2]);
    printf("  o: %d\n", args_cons.count_vocales[3]);
    printf("  u: %d\n", args_cons.count_vocales[4]);

    // limpiamos recursos
    // sem_destroy libera los recursos del semaforo anonimo
    // no hace falta sem_unlink porque estos semaforos no tienen nombre
    // en el kernel (desaparecen solos cuando el proceso termina)
    // aun asi los destruimos explicitamente
    sem_destroy(&mutex);
    sem_destroy(&vacias);
    sem_destroy(&llenas);

    fclose(ftexto);

    exit(0);
}

// hilo productor
// funcion que ejecuta el hilo productor
// recibe un void * que casteamos a args_hilo* para acceder al archivo
// de texto y al contador de vocales
// realiza ITERACIONES ciclos y luego activa prod_fin para avisar al consumidor
void *hilo_productor(void *arg)
{
    args_hilo *a = (args_hilo *)arg;

    // semilla para los sleeps aleatorios de las ultimas iteraciones
    srand((unsigned int)time(NULL));

    for (int i = 0; i < ITERACIONES; i++)
    {
        // producimos el item fuera de la region critica
        // produce_item no accede al buffer (solo lee del archivo que
        // es privado del productor por lo que no necesita proteccion)
        char item = produce_item(a->ftexto, a->count_vocales);

        // si llegamos al EOF antes de completar las iteraciones rebobinamos
        if (item == '\0')
        {
            rewind(a->ftexto);
            item = produce_item(a->ftexto, a->count_vocales);
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

        // se bloquea si no hay huecos libres en el buffer
        // cuando se desbloquea decrementa vacias en 1 (reserva un hueco)
        sem_wait(&vacias);

        // garantiza que solo un hilo este en la seccion critica a la vez
        // si el consumidor ya esta dentro esperamos
        sem_wait(&mutex);

        insert_item(item); // acceso exclusivo al buffer

        sem_post(&mutex); // liberamos la exclusion mutua

        // incrementa llenas en 1 indicando que hay un nuevo elemento
        // disponible si el consumidor estaba bloqueado en sem_wait(&llenas)
        // esto lo despierta
        sem_post(&llenas);

        // fin de la region critica
    }

    // avisamos al consumidor de que no habra mas datos
    // el consumidor solo la lee cuando llenas == 0
    // es decir cuando ya no queda ningun elemento en el buffer
    // por lo que no puede haber conflicto con insert_item
    comp.prod_fin = 1;
    printf("[PROD] Terminado. prod_fin = 1\n");

    return NULL; // pthread_join en main recibira este valor de retorno
}

// hilo consumidor
// funcion que ejecuta el hilo consumidor 
// realiza ITERACIONES ciclos extrayendo un elemento del buffer en cada uno
void *hilo_consumidor(void *arg)
{
    args_hilo *a = (args_hilo *)arg;

    // semilla distinta a la del productor para los sleeps alaeatorios
    srand((unsigned int)time(NULL)^(unsigned int)pthread_self());

    for (int i = 0; i < ITERACIONES; i++)
    {
        // region critica

        // se bloquea si el buffer esta vacio
        // cuando se desbloquea decrementa llenas en 1
        sem_wait(&llenas);

        sem_wait(&mutex); // exclusion mutua sobre el buffer

        char item = remove_item();

        sem_post(&mutex); // liberamos la exclusion mutua

        // un hueco ha quedado libre si el productor estaba
        // bloqueado esperando espacio esto lo despierta
        sem_post(&vacias);
        // fin de la region critica

        // procesamos el item fuera de la region critica
        consume_item(item, a->count_vocales);

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
 
    printf("[CONS] Terminado.\n");
    return NULL;
}

// funcion que lee un unico caracter del archivo y actualiza el contador de vocales
// del productor
// devuelve \0 al llegar al EOF
// no accede al buffer compartido (no necesita proteccion con semaforos)
char produce_item(FILE *f, int count_vocales[5])
{
    int c = fgetc(f);
    if (c == EOF)
        return '\0';

    char lc = tolower((char)c);
    if      (lc == 'a') count_vocales[0]++;
    else if (lc == 'e') count_vocales[1]++;
    else if (lc == 'i') count_vocales[2]++;
    else if (lc == 'o') count_vocales[3]++;
    else if (lc == 'u') count_vocales[4]++;
 
    return (char)c;
}

// funcion que inserta el caracter en la cima de la pila LIFO
// siempre se llama dentro de la region critica por lo que el acceso
// a comp es seguro
void insert_item(char c)
{
    if (comp.top < N)
    {
        comp.buffer[comp.top] = c;
        comp.top++;
        printf("[PROD] Insertado '%c'. Top: %d\n", c, comp.top);
    }
}

// funcion que retira el elemento de la cima de la pila LIFO y lo 
// reemplaza por -
// siempre se llama dentt¡ro de la region critica igual que insert_item
char remove_item(void)
{
    char c = '\0';

    if (comp.top > 0)
    {
        int top_actual = comp.top - 1;
        c = comp.buffer[top_actual];
        comp.buffer[top_actual] = '-';
        comp.top = top_actual;
        printf("[CONS] Eliminado '%c'. Top: %d\n", c, comp.top);
    }

    return c;
}

// funcion que actualiza el contador de vocales del consumidor con el
// caracter retirado
// no accede al buffer compartido (no necesita proteccion con semaforos)
void consume_item(char c, int count_vocales[5])
{
    if (c == '\0' || c == '-')
        return;
 
    c = tolower(c);
    if      (c == 'a') count_vocales[0]++;
    else if (c == 'e') count_vocales[1]++;
    else if (c == 'i') count_vocales[2]++;
    else if (c == 'o') count_vocales[3]++;
    else if (c == 'u') count_vocales[4]++;
}