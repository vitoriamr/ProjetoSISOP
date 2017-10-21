#include <stdio.h>
#include <stdlib.h>
#include "../include/support.h"
#include "../include/cdata.h"
#include <ucontext.h>
#define MEM 64000

typedef struct s_sem {
    int count;  // indica se recurso está ocupado ou não (livre > 0, ocupado = 0)
    PFILA2 fila;    // ponteiro para uma fila de threads bloqueadas no semáforo
} csem_t;

typedef struct join_list {
    int waiting;
    int ended;
} joinChecklist;

PFILA2 joinList;
PFILA2 executando, bloqueado;
// 0 = maior prioridade
PFILA2 aptos, termino;
// T ID
int t_tid=0;

ucontext_t c_thread_finish, c_current;

TCB_t *finder, *current_tcb;

int InsertByPrio(PFILA2 pfila, TCB_t *tcb) { /** DEPOIS REMOVER E COLOCAR REFERENCIA CERTA **/
    TCB_t *tcb_it;

    // pfile vazia?
    if (FirstFila2(pfila)==0) {
        do {
            tcb_it = (TCB_t *) GetAtIteratorFila2(pfila);
            if (tcb->prio < tcb_it->prio) {
                return InsertBeforeIteratorFila2(pfila, tcb);
            }
        } while (NextFila2(pfila)==0);
    }
    return AppendFila2(pfila, (void *)tcb);
}

// move o elemento de executando para: queue = 1 -> aptos || queue = 3 -> bloqueado
// retorna tid do elemento movido para bloqueado/aptos
int move_exec_elements(int queue){ /** REFEITO **/
    int t = FirstFila2(executando);
    int tid = -1;
    if(t == 0){
        finder = (TCB_t*)GetAtIteratorFila2(executando);
        if(finder){
            switch(queue){
                case 1:
                    InsertByPrio(aptos,finder);
                    finder->state = queue;
                break;
                case 3:
                    AppendFila2(bloqueado,finder);
                    finder->state = queue;
                break;
                case 4:
                    AppendFila2(termino,finder);
                    finder->state = queue;
                break;
            }
            tid = finder->tid;
            DeleteAtIteratorFila2(executando);
            FirstFila2(executando);
        }
    }
    return tid;
}

// procura se a thread esperada existe na fila "fila"
int findOnQueue(int tid, PFILA2 fila){ /** REFEITO **/
    int t;
    t = FirstFila2(fila);
    if(t == 0){ // ou seja, existe algo na fila
        while(t != -3){ // -3 = final da fila
            finder = (TCB_t*)GetAtIteratorFila2(fila);
            if(tid == finder->tid){
                FirstFila2(fila);
                return 1;
            }
            t = NextFila2(fila);
        }
    }
    FirstFila2(fila);
    return 0;
}

// procura se a thread esperada existe na fila "fila"
int findOnJoinlist(int tid, PFILA2 fila){ /** CRIADO **/
    int t;
    t = FirstFila2(fila);
    joinChecklist *findJoin;
    if(t == 0){ // ou seja, existe algo na fila
        while(t != -3){ // -3 = final da fila
            findJoin = (joinChecklist*)GetAtIteratorFila2(fila);
            if(tid == findJoin->ended){ // Verifica se thread que acabou e uma das threads esperadas na lista
                DeleteAtIteratorFila2(fila);
                FirstFila2(fila);
                return findJoin->waiting;
            }
            t = NextFila2(fila);
        }
    }
    FirstFila2(fila);
    return -1;
}

// move elemento de apto pra executando
void move_apto_exec(PFILA2 fila){ /** REVISADO **/
    FirstFila2(fila);
    finder = (TCB_t*)GetAtIteratorFila2(fila);
    DeleteAtIteratorFila2(fila);
    FirstFila2(fila);
    AppendFila2(executando, finder);
}

// move elemento de bloqueado para apto
int move_block_apto(int blockTid){ /** REVISADO **/
    int t;
    t = FirstFila2(bloqueado);
    if(t == 0){ // ou seja, existe algo na fila
        while(t != -3){ // -3 = final da fila
            finder = (TCB_t*)GetAtIteratorFila2(bloqueado);
            if(blockTid == finder->tid){
                InsertByPrio(aptos,finder);
                DeleteAtIteratorFila2(bloqueado);
                FirstFila2(bloqueado);
                return 1;
            }
            t = NextFila2(bloqueado);
        }
    }
    FirstFila2(bloqueado);
    return 0;
}

// general purpose escalo--FUCK
int escalonador(int mode){ /** REPENSANDO -> avaliar retornos negativos caso erros **/
    /** Relacionado a mover filas **/
    //printf("exec escalonador mode:%d\n", mode);
    switch(mode){
        // yield
        case 1:
            // Move thread de executando para apto
            move_exec_elements(1);
        break;
        // join & cwait
        case 2:
            // Move thread de executando para bloqueado
            move_exec_elements(3);
        break;
        // csignal
        case 3:
            if(finder)
                move_block_apto(finder->tid); // finder foi encontrado na funcao csignal para poder fornecer o TID da thread a ser removida de blocked
            return 0; // csignal nao retira thread de executando
        break;
        case 4:
        case 5:
        break;
    }
    dispatcher();
    return 0;
}

int dispatcher(){
    if(FirstFila2(aptos) == 0){/** Relacionado com execucao **/
        // Seleciona thread
        move_apto_exec(aptos); // Move thread de apto para executando
        // Recuperando contexto
        FirstFila2(executando);
        finder = (TCB_t*)GetAtIteratorFila2(executando);
        current_tcb = finder;
        // Efetua troca de contexto
        if(finder){
            printf("exec tid %d\n", finder->tid);
            setcontext(&finder->context);
        }
    }
    return 0;
}

void threadFinished(){
    /** Relacionado com retorno de execucoes **/
    // Move the exec pra termino
    move_exec_elements(4);
    // Verificar joinList e alterar a thread esperando
    int tid = findOnJoinlist(finder->tid,joinList);
    if(tid != -1){
        move_block_apto(tid);
    }
    dispatcher();
}

// inicializa as estruturas globais de apoio
void startHelpers(){ /** REFEITO **/
    getcontext(&c_current);
    c_current.uc_link = &c_thread_finish;
    c_current.uc_stack.ss_sp=malloc(MEM);
    c_current.uc_stack.ss_size=MEM;

    current_tcb = (TCB_t*)(malloc(sizeof(TCB_t)));
    current_tcb->tid = 0;
    current_tcb->state = 0;
    current_tcb->prio = 3;
    current_tcb->context = c_current;

    executando = (malloc(sizeof(PFILA2)));
    bloqueado = (malloc(sizeof(PFILA2)));
    aptos = (malloc(sizeof(PFILA2)));
    termino = (malloc(sizeof(PFILA2)));
    joinList = (malloc(sizeof(PFILA2)));

    CreateFila2(executando);
    CreateFila2(bloqueado);
    CreateFila2(aptos);
    CreateFila2(termino);
    CreateFila2(joinList);

    finder = (TCB_t*)(malloc(sizeof(TCB_t)));

    //contexto escalonador
    getcontext(&c_thread_finish);
    c_thread_finish.uc_link = 0;
    c_thread_finish.uc_stack.ss_sp=malloc(MEM);
    c_thread_finish.uc_stack.ss_size=MEM;
    makecontext(&c_thread_finish, (void*)threadFinished, 0);

    AppendFila2(executando, current_tcb);

    FirstFila2(executando);
    FirstFila2(bloqueado);
    FirstFila2(aptos);
    FirstFila2(termino);
    FirstFila2(joinList);


}

// inicializa todas as estruturas para uma thread e a insere na fila apropriada a sua prioridade
int ccreate (void (*start)(void), void *arg, int prio){ /** REFEITO **/
    int r;
    //inicializa estruturas
    if(t_tid == 0){
        startHelpers();
    }
    t_tid++;
    //inicializa TCB
    TCB_t *t_helper = (TCB_t*)(malloc(sizeof(TCB_t)));

    //cria contexto
    getcontext(&t_helper->context);
    t_helper->context.uc_link = &c_thread_finish;
    t_helper->context.uc_stack.ss_sp=malloc(MEM);
    t_helper->context.uc_stack.ss_size=MEM;
    makecontext(&t_helper->context, start, 1, arg);

    //seta prioridade
    t_helper->prio = prio;

    //seta tid
    t_helper->tid = t_tid;

    //estado
    t_helper->state = 0;

    // Insere fila aptos
    r = InsertByPrio(aptos, t_helper);

    return r == 0 ? t_helper->tid : -1;
}

// altera prioridade de uma thread em qualquer uma das filas
int csetprio(int tid, int prio){ /** APROVADO **/

}

// libera voluntariamente o CPU e chama a proxima thread para executar
int cyield(){ /** REFEITO (falta escalonador) **/
    /**
    - mover de executando para apto
    - chamar escalonador para selecionar proxima thread
    **/
    //puts("exec cyield");
    int flag = 0, x;
    finder = (TCB_t*)GetAtIteratorFila2(executando);
    if(getcontext(&finder->context) == -1)
        return -1;
    if(flag == 0){
        flag = 1;
        x = escalonador(1);
    }
    return x;
}

// recebe um tid e a thread que chamou cjoin fica esperando o fim da thread indicada por cjoin
int cjoin(int waitTid){ /** REFEITO (falta escalonador) **/
    /**
    passos:
        - salvar o waitTid numa lista (garantir que é unico)
            > waitTid-List : waitTid, tid de executando
        - chamar escalonador para selecionar proxima thread
    ESCALONADOR:
        - mover a thread executando para bloqueado
        - quando thread waitTid acabar, move  thread bloqueada para apto
    **/
    int flag = 0;
    // Verificar se thread esperada existe
    if(findOnQueue(waitTid,aptos) || findOnQueue(waitTid,bloqueado)){
        // verificar se ainda nao existe na joinChecklist
        if(!(findOnQueue(waitTid,joinList))){
            // Salva tid executando e waitTid numa lista
            joinChecklist *list;
            list = (joinChecklist*)malloc(sizeof(joinChecklist));

            finder = (TCB_t*)GetAtIteratorFila2(executando);
            if(getcontext(&finder->context) == -1){
                return -1;
            }
            if(flag == 0 && finder){
                flag = 1;
                list->waiting = finder->tid;
                list->ended = waitTid;
                AppendFila2(joinList,list);
            }
            escalonador(2); // ainda temos que ver se ela eventualmente realmente retorna 0 e como o escalonador vai trabalhar
            return 0;
        }
        else return -1;
    }
    else return -1;
}

// inicializa estruturas do tipo semaforo
int csem_init(csem_t *sem, int count){ /** TESTAR **/
    //inicializacao da estrutra csem_t
    sem->count = count;
    sem->fila = (PFILA2*)(malloc(sizeof(PFILA2)));
    if(CreateFila2(sem->fila) == 0){
        FirstFila2(sem->fila);
        return 0;
    }
    return -1;
}

// se um recurso estiver livre, atribui-o a thread
int cwait(csem_t *sem){ /** TESTAR **/
    if(sem->count > 0){ //recurso livre? -> count > 0
        sem->count = sem->count - 1;
        return 0;
    }                               //continua execucao
    else {
        sem->count = sem->count - 1;
        // Coloca thread na fila de espera do semaforo
        finder = (TCB_t*)GetAtIteratorFila2(executando);
        int flag = 0;
        if(finder){
            AppendFila2(sem->fila, finder);
            getcontext(&finder->context);
        }
        // Solicitar recurso
        if(flag == 0){
            flag = 1;
            escalonador(2);
        }
    }
}

// libera um recurso que a thread estava usando e passa o primeiro da sua fila de bloqueados para apto
int csignal(csem_t *sem){ /** TESTAR **/
    // Libera recurso
    sem->count = sem->count + 1;
    // Verificar thread esperando recurso
    if(FirstFila2(sem->fila) == 0){
        finder = (TCB_t*)GetAtIteratorFila2(sem->fila);
        escalonador(3);
        return 0;
    }
    else return 0;
}

// identidade dos cancer filhos da puta que fizeram esse trabalho desgracado
int cidentify (char *name, int size){
    if(name == NULL){
        name = malloc(sizeof(char) * size);
    }

    if(size <= 0){
        printf("[cidentify] Erro: o tamanho não pode ser negativo.\n");
        return -1;
    }

    char student[] = "Gustavo Paz da Rosa - 00206615 \n Italo Joaquim Minussi Franco - 00242282 \n Vitoria Mainardi Rosa - 00242252\n";
    int i = 0;

    while (i < size && i < sizeof(student)){
        name[i] = student[i];
        i++;
    }

    return 0;
}
