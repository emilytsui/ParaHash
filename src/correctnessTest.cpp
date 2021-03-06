#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <string>
#include <cassert>
#include <pthread.h>

#include "tools/cycle_timer.h"
#include "seq_hash_table.h"
#include "fg_hash_table.h"
#include "mem_leak_hash_table.h"

enum Instr {
    insert,
    del,
    lookup
};

static int numThreads;
static std::vector<std::pair<Instr, std::pair<int, int> > > input;
SeqHashTable<int, int>* baseline;
FgHashTable<int, int>* htable;
MemLeakHashTable<int, int>* lockFreeTable;

const char *args[] = {"tests/correctness1.txt",
                      "tests/correctness2.txt"};
std::vector<std::string> testfiles(args, args + sizeof(args)/sizeof(args[0]));

int hash(int tag) {
    int temp = tag;
    int hashVal = 7;
    while(temp != 0) {
        hashVal *= 31;
        hashVal += temp % 10;
        temp /= 10;
    }
    return abs(hashVal);
}

void parseText(const std::string &filename)
{
    std::ifstream infile;
    infile.open(filename.c_str());
    input.resize(0);
    while(!infile.eof())
    {
        std::string str;
        std::pair<Instr, std::pair<int, int> > task;
        getline(infile, str);
        if (str.length() > 0)
        {
            char instr = str[0];
            switch(instr) {
            case 'L':
                task.first = lookup;
                break;
            case 'I':
                task.first = insert;
                break;
            case 'D':
                task.first = del;
                break;
            default:
                break;
            }

            std::pair<int, int> keyval;
            std::string rest = str.substr(2);
            int spcIdx = rest.find_first_of(' ');
            keyval.first = atoi(rest.substr(0, spcIdx).c_str());
            keyval.second = atoi(rest.substr(spcIdx + 1).c_str());
            task.second = keyval;
            input.push_back(task);
        }
    }
}

void* lockFreeRun(void *arg) {
    // printf("In delete Optimal\n");
    int id = *(int*)arg;
    int instrPerThread = input.size() / numThreads;
    int start = instrPerThread * id;
    int end = (start + instrPerThread < input.size()) ? (start + instrPerThread) : input.size();
    for (int i = start; i < end; i++)
    {
        std::pair<Instr, std::pair<int, int> > instr = input[i];
        switch(instr.first)
        {
            case insert:
                // printf("Thread %d insert: %d\n", id, instr.second.first);
                lockFreeTable->insert(instr.second.first, instr.second.second);
                break;
            case del:
                // printf("Thread %d delete: %d\n", id, instr.second.first);
                lockFreeTable->remove(instr.second.first);
                break;
            case lookup:
                // printf("Thread %d lookup: %d\n", id, instr.second.first);
                lockFreeTable->find(instr.second.first);
                break;
            default:
                break;
        }
    }
    pthread_exit(NULL);
}

void* fgRun(void *arg)
{
    int id = *(int*)arg;
    int instrPerThread = input.size() / numThreads;
    int start = instrPerThread * id;
    int end = (start + instrPerThread < input.size()) ? (start + instrPerThread) : input.size();
    for (int i = start; i < end; i++)
    {
        std::pair<Instr, std::pair<int, int> > instr = input[i];
        switch(instr.first)
        {
            case insert:
                htable->insert(instr.second.first, instr.second.second); // Can Fail
                break;
            case del:
                htable->remove(instr.second.first); // Can Fail
                break;
            case lookup:
                htable->find(instr.second.first); // Can Fail
                break;
            default:
                break;
        }
    }
    pthread_exit(NULL);
}

void seqRun(SeqHashTable<int, int>* htable)
{
    for (int i = 0; i < input.size(); i++)
    {
        std::pair<Instr, std::pair<int, int> > instr = input[i];
        switch(instr.first)
        {
            case insert:
                htable->insert(instr.second.first, instr.second.second); // Can Fail
                break;
            case del:
                htable->remove(instr.second.first); // Can Fail
                break;
            case lookup:
                htable->find(instr.second.first); // Can Fail
                break;
            default:
                break;
        }
    }
}

void testLockFreeCorrectness(SeqHashTable<int, int>* baseline, MemLeakHashTable<int, int>* htable) {
    for (int i = 0; i < baseline->table_size; i++)
    {
        LLNode<int, int>* curr = baseline->table[i];
        while(curr != NULL)
        {
            LLNode<int, int>* res = htable->find(curr->get_key());
            if(res == NULL || res->get_data() != curr->get_data())
            {
                printf("Incorrect: Lock-free Hash Table doesn't contain (%d, %d)\n", curr->get_key(), curr->get_data());
            }
            curr = curr->get_next();
        }
    }
    for (int j = 0; j < htable->table_size; j++)
    {
        LLNode<int, int>* curr = htable->table[j]->get_next();
        // printf("Next pointer: %p\n", curr);
        while(curr != NULL)
        {
            LLNode<int, int>* res = baseline->find(curr->get_key());
            // printf("%p\n", curr);
            if(res == NULL || res->get_data() != curr->get_data())
            {
                printf("Incorrect: Lock-free Hash Table contains additional elem (%d, %d)\n", curr->get_key(), curr->get_data());
            }
            curr = curr->get_next();
        }
    }
    // printf("Completed delete optimal correctness\n");
}

void testFgCorrectness(SeqHashTable<int, int>* baseline, FgHashTable<int, int>* htable)
{
    for (int i = 0; i < baseline->table_size; i++)
    {
        LLNode<int, int>* curr = baseline->table[i];
        while(curr != NULL)
        {
            LLNode<int, int>* res = htable->find(curr->get_key());
            if(res == NULL || res->get_data() != curr->get_data())
            {
                printf("Incorrect: Lock-based Hash Table doesn't contain (%d, %d)\n", curr->get_key(), curr->get_data());
            }
            curr = curr->get_next();
        }
    }
    for (int j = 0; j < htable->table_size; j++)
    {
        LLNode<int, int>* curr = htable->table[j];
        while(curr != NULL)
        {
            LLNode<int, int>* res = baseline->find(curr->get_key());
            if(res == NULL || res->get_data() != curr->get_data())
            {
                printf("Incorrect: Lock-based Hash Table contains additional elem (%d, %d)\n", curr->get_key(), curr->get_data());
            }
            curr = curr->get_next();
        }
    }
}

int main() {

    pthread_t threads[16];
    int ids[16];
    for (uint z = 0; z < 16; z++)
    {
        ids[z] = z;
    }
    for (uint i = 0; i < testfiles.size(); i++) {
        parseText(testfiles[i].c_str());
        baseline = new SeqHashTable<int, int>(10000, &hash);
        seqRun(baseline);
        printf("Correctness Testing file: %s for fine-grained hash table\n", testfiles[i].c_str());
        for (uint j = 1; j <= 16; j *= 2)
        {
            htable = new FgHashTable<int, int>(10000, &hash);
            numThreads = j;
            for (uint id = 0; id < j; id++)
            {
                pthread_create(&threads[id], NULL, fgRun, &ids[id]);
            }
            for (uint id = 0; id < j; id++)
            {
                pthread_join(threads[id], NULL);
            }
            testFgCorrectness(baseline, htable);
            delete(htable);
        }
        printf("Correctness Testing file: %s for lock-free hash table with memory leaks\n", testfiles[i].c_str());
        for (uint j = 1; j <= 16; j *= 2)
        {
            lockFreeTable = new MemLeakHashTable<int, int>(10000, &hash);
            numThreads = j;
            for (uint id = 0; id < j; id++)
            {
                pthread_create(&threads[id], NULL, lockFreeRun, &ids[id]);
            }
            for (uint id = 0; id < j; id++)
            {
                pthread_join(threads[id], NULL);
            }
            testLockFreeCorrectness(baseline, lockFreeTable);
            delete(lockFreeTable);
        }
        delete(baseline);
    }
    printf("Correctness Tests Complete!\n");
}