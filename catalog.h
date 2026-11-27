#pragma once

#include <string>

using std::string;

enum Status { Unknown, Announced, Production, Released, Cancelled };

struct Record {
    int id = 0, type = 1, age = 0, status = Unknown;
    string title, genre, director;  // пустая строка означает, что сведения отсутствуют
    string premiere;    // дата в формате ДД-ММ-ГГГГ или пустая строка, если дата неизвестна
    double rating = -1;  // оценка отсутствует при значении -1
    int minutes = 0;     // длительность неизвестна при значении 0
};

struct Node {
    Record record;
    Node* prev;
    Node* next;
    string titleKey;  // ключ сортировки обновляется вместе с названием
};

struct List {
    Node* first = nullptr;
    Node* last = nullptr;
    int count = 0;  // обновляется при добавлении, удалении и замене списка
    List() = default;
    List(const List&) = delete;
    List& operator=(const List&) = delete;
    ~List();

    Node* find(int id) const;
    void append(const Record& record);
    void erase(Node* node);
};

struct EndOfInput {};
struct Filter;

string today();
void add(List& list, const string& currentDate);
void search(const List& list);
bool edit(List& list, const string& currentDate);
bool removeRecord(List& list);
int printRecords(const List& list, const Filter* filter = nullptr);
bool sortCatalog(List& list);
bool save(const List& list);
bool load(List& list, const string& currentDate);
void filterCatalog(const List& list);

bool confirmReplace(const List& list, bool& dirty);
int readMenu(int count);
void pauseMenu();
