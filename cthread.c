#include <stdio.h>
#include <stdlib.h>
#include "../include/support.h"
#include "../include/cdata.h"
#include <ucontext.h>
#define MEM 64000

typedef struct s_sem {
    int count;  // indica se recurso está ocupado ou não (livre > 0, ocupado = 0)
    PFILA2  fila;   // ponteiro para uma fila de threads bloqueadas no semáforo
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

ucontext_t c_escalonador;

TCB_t *finder;

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
    return 0;
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
    switch(mode){
        // yield
        case 1:
            // Move thread de executando para apto
            move_exec_elements(1);
        break;
        // join
        case 2:
            // Move thread de executando para bloqueado
            move_exec_elements(3);
        break;
        // tbd
        case 3:
        case 4:
        case 5:
        break;
    }
    /** Relacionado com execucao **/
    // Seleciona thread
    move_apto_exec(aptos); // Move thread de apto para executando
    // Recuperando contexto
    FirstFila2(executando);
    finder = (TCB_t*)GetAtIteratorFila2(executando);
    // Efetua troca de contexto
    if(finder){ /** Controlar swapback para sync de termino, mudar exec + termino para dispatcher, evitar recursao**/
        swapcontext(&c_escalonador,&finder->context);
    }
    /** Relacionado com retorno de execucoes **/
    // Move the exec pra termino
    move_exec_elements(4);
    // Verificar joinList e alterar a thread esperando
    int tid = findOnJoinlist(finder->tid,joinList);
    if(tid){
        move_block_apto(tid);
    }
    // Proximo // mais tarde -> verificar se a fila nao esta vazia antes de escalonar
}

// inicializa as estruturas globais de apoio
void startHelpers(){ /** REFEITO **/
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
    getcontext(&c_escalonador);
    c_escalonador.uc_link = 0;
    c_escalonador.uc_stack.ss_sp=malloc(MEM);
    c_escalonador.uc_stack.ss_size=MEM;
    makecontext(&c_escalonador, (void*)escalonador, 0);

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
    t_helper->context.uc_link = &c_escalonador;
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

// libera voluntariamente o CPU e chama a proxima thread para executar
int cyield(){ /** REFEITO (falta escalonador) **/
    /**
    - mover de executando para apto
    - chamar escalonador para selecionar proxima thread
    **/
    int flag = 0, x;

    finder = (TCB_t*)GetAtIteratorFila2(executando);
    if(getcontext(&(finder->context)) == -1)
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
    // Verificar se thread esperada existe
    if(findOnQueue(waitTid,aptos) || findOnQueue(waitTid,bloqueado)){
        // verificar se ainda nao existe na joinChecklist
        if(!(findOnQueue(waitTid,joinList))){
            // Salva tid executando e waitTid numa lista
            joinChecklist *list;
            finder = (TCB_t*)GetAtIteratorFila2(executando);
            if(finder){
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
