#include "catalog.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;
namespace fs = filesystem;

// запись и собственный двусвязный список
const char* types[] = {"", "Фильм", "Сериал", "Короткометражка"};
const char* ages[] = {"Неизвестно", "0+", "6+", "12+", "16+", "18+"};
const char* statuses[] = {"Неизвестно", "Анонсирован", "В производстве",
                          "Вышел", "Отменён до выхода"};
const string missingValue = "нет данных";
const char* typePrompt = "Тип (1 — фильм, 2 — сериал, 3 — короткометражка): ";
const char* agePrompt = "Возраст (0 — неизвестно, 1 — 0+, 2 — 6+, 3 — 12+, 4 — 16+, 5 — 18+): ";
const char* datePrompt = "Премьера ДД-ММ-ГГГГ (например 16-07-2010, 0 — нет даты): ";
const char* retryPrompt = "1 — повторить, 0 — вернуться в меню: ";
const char* reviewMessage = "Уточните плановую дату или подтвердите выход в поле «статус».\n";

string titleSortKey(const string& title);

bool sameRecord(const Record& a, const Record& b) {
    return a.id == b.id && a.type == b.type && a.age == b.age && a.status == b.status
        && a.title == b.title && a.genre == b.genre && a.premiere == b.premiere && a.director == b.director
        && a.rating == b.rating && a.minutes == b.minutes;
}

List::~List() {
    while (last) erase(last);
}

Node* List::find(int id) const {
    for (Node* p = first; p; p = p->next)
        if (p->record.id == id) return p;
    return nullptr;
}

void List::append(const Record& record) {
    Node* p = new Node{record, last, nullptr, titleSortKey(record.title)};
    if (last) last->next = p;
    else first = p;
    last = p;
    ++count;
}

void List::erase(Node* p) {
    if (p->prev) p->prev->next = p->next;
    else first = p->next;
    if (p->next) p->next->prev = p->prev;
    else last = p->prev;
    delete p;
    --count;
}

// ввод, календарь и согласованность полей
string trim(const string& text) {
    const string::size_type first = text.find_first_not_of(" \r\n\t");
    if (first == string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}

// приводим английские и русские буквы к нижнему регистру за один проход по UTF-8
string lower(string text) {
    for (size_t i = 0; i < text.size(); ++i) {
        char& first = text[i];
        if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
        if (first != '\xD0' || i + 1 == text.size()) continue;

        char& second = text[i + 1];
        const unsigned char byte = static_cast<unsigned char>(second);
        if (byte >= 0x90 && byte <= 0x9F) {  // для А–П меняется второй байт
            second = static_cast<char>(byte + 0x20);
        } else if (byte == 0x81 || (byte >= 0xA0 && byte <= 0xAF)) {  // для Ё и Р–Я первый байт становится D1
            first = '\xD1';
            second = byte == 0x81 ? '\x91' : static_cast<char>(byte - 0x20);
        }
    }
    // приводим раздельно набранные ё и й к тем же буквам, что используются в обычном тексте
    for (size_t i = text.find('\xCC'); i != string::npos; i = text.find('\xCC', i + 1)) {
        if (i < 2 || i + 1 == text.size() || text[i - 2] != '\xD0') continue;
        if (text[i - 1] == '\xB5' && text[i + 1] == '\x88') text.replace(i - 2, 4, "ё");
        else if (text[i - 1] == '\xB8' && text[i + 1] == '\x86') text.replace(i - 2, 4, "й");
    }
    return text;
}

string titleSortKey(const string& title) {
    const string text = lower(title);
    string key;
    for (size_t i = 0; i < text.size(); ++i) {
        // делим букву «е» на две соседние позиции: «е», затем «ё»
        if (text.compare(i, 2, "е") == 0 || text.compare(i, 2, "ё") == 0) {
            key += text.compare(i, 2, "е") == 0 ? "е\x01" : "е\x02";
            ++i;
        } else {
            key += text[i];
        }
    }
    return key;
}

string today() {
    const time_t now = time(nullptr);
    const tm* local = localtime(&now);
    char date[11]{};
    if (local) strftime(date, sizeof date, "%d-%m-%Y", local);
    return date;
}

// в формате ДД-ММ-ГГГГ сравниваем по порядку год, месяц и день
int compareDates(const string& a, const string& b) {
    if (a.empty() || b.empty()) return a.compare(b);
    const int year = a.compare(6, 4, b, 6, 4);
    if (year != 0) return year;
    const int month = a.compare(3, 2, b, 3, 2);
    return month != 0 ? month : a.compare(0, 2, b, 0, 2);
}

bool allowsStatus(const Record& r, int status, const string& currentDate, bool loading = false) {
    // при загрузке сохраняем просроченные плановые даты для ручного уточнения
    if (status < Unknown || status > Cancelled) return false;
    if (status == Released) return compareDates(r.premiere, currentDate) <= 0;
    if (r.rating != -1) return false;
    if (!loading && !r.premiere.empty() && compareDates(r.premiere, currentDate) < 0) return false;
    return status != Cancelled || r.premiere.empty();
}

bool validText(const string& text) {
    for (unsigned char c : text)
        if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}

constexpr int ConsoleLineLimit = 4096;
constexpr int FileLineLimit = 2 * ConsoleLineLimit + 128;
enum LineResult { LineOk, LineEnd, LineTooLong, LineError };

LineResult readLine(istream& in, string& line, int limit, bool discardRemainder = false) {
    // для консоли пропускаем остаток длинной строки, чтобы следующий ввод начался с новой строки
    char buffer[FileLineLimit + 3];
    in.getline(buffer, limit + 3);
    if (in.bad()) return LineError;
    if (in.fail()) {
        if (in.eof()) return in.gcount() == 0 ? LineEnd : LineError;
        if (discardRemainder) {
            in.clear();
            in.ignore(numeric_limits<streamsize>::max(), '\n');
        }
        return in.bad() ? LineError : LineTooLong;
    }
    line.assign(buffer, in.gcount() - (in.eof() ? 0 : 1));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line.size() > static_cast<size_t>(limit) ? LineTooLong : LineOk;
}

string ask(const string& prompt, bool required = false, bool trimInput = true) {
    string text;
    bool valid;
    do {
        cout << prompt << flush;
        const LineResult result = readLine(cin, text, ConsoleLineLimit, true);
        if (result == LineEnd) throw EndOfInput{};
        if (result == LineError) throw runtime_error("сбой чтения из консоли");
        valid = result == LineOk && validText(text) && (!required || !trim(text).empty());
        if (result == LineTooLong) cout << "Слишком длинная строка. Максимум " << ConsoleLineLimit << " байт.\n";
        else if (!valid) cout << "Введите корректный текст.\n";
    } while (!valid);
    return trimInput ? trim(text) : text;
}

string readOptionalText(const string& prompt) {
    string text;
    do {
        text = ask(prompt);
        if (text.empty()) cout << "Введите текст или 0.\n";
    } while (text.empty());
    return text == "0" ? "" : text;
}

// после числа разрешены только пробельные символы
template<class Number>
bool parseNumber(const string& text, Number& value) {
    istringstream input(text);
    if (!(input >> value)) return false;
    input >> ws;
    return input.eof();
}

int readInt(const string& prompt, int min, int max) {
    int value;
    bool valid;
    do {
        valid = parseNumber(ask(prompt), value) && value >= min && value <= max;
        if (!valid) cout << "Введите целое число от " << min << " до " << max << ".\n";
    } while (!valid);
    return value;
}

bool parseRatingNumber(string text, double& value) {
    for (char& c : text) if (c == ',') c = '.';
    const string::size_type dot = text.find('.');
    const bool validFormat = text.find_first_not_of("0123456789.") == string::npos
        && (dot == string::npos || (dot > 0 && dot + 2 == text.size()));
    return validFormat && parseNumber(text, value) && value >= 0 && value <= 10;
}

double readRating(const string& prompt, double min = 0) {
    double value;
    bool valid;
    do {
        const string text = ask(prompt);
        if (text == "0") return -1;
        valid = parseRatingNumber(text, value) && value >= min;
        if (!valid)
            cout << "Введите число от " << min << " до 10 с точностью до десятых или 0, чтобы пропустить.\n";
    } while (!valid);
    return value;
}

bool validDate(const string& text) {
    if (text.size() != 10 || text[2] != '-' || text[5] != '-') return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (i != 2 && i != 5 && (text[i] < '0' || text[i] > '9')) return false;
    const int day = stoi(text.substr(0, 2));
    const int month = stoi(text.substr(3, 2));
    const int year = stoi(text.substr(6, 4));
    if (year == 0 || month < 1 || month > 12) return false;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day >= 1 && day <= days[month - 1] + (month == 2 && leap);
}

// по умолчанию граница нижняя, а при upperLimit — верхняя
string readDate(const string& prompt, const string& limit = "", bool upperLimit = false) {
    string date;
    bool valid;
    do {
        date = ask(prompt);
        if (date == "0") return {};
        valid = validDate(date);
        if (valid && !limit.empty()) {
            const int order = compareDates(date, limit);
            valid = upperLimit ? order <= 0 : order >= 0;
        }
        if (!valid) {
            cout << "Введите существующую дату ДД-ММ-ГГГГ";
            if (!limit.empty()) cout << (upperLimit ? " не позже " : " не раньше ") << limit;
            cout << " или 0, чтобы пропустить.\n";
        }
    } while (!valid);
    return date;
}

int choose(const char* prompt, const char* const labels[], int count) {
    // пустой указатель означает, что этот пункт недоступен
    for (int i = 0; i < count; ++i)
        if (labels[i]) cout << i << " — " << labels[i] << '\n';
    int value;
    bool valid;
    do {
        valid = parseNumber(ask(prompt), value) && value >= 0 && value < count && labels[value];
        if (!valid) cout << "Выберите один из показанных вариантов.\n";
    } while (!valid);
    return value;
}

int readId(const List& list, int oldId = 0) {
    int id;
    bool taken;
    do {
        id = readInt("ID (уникальное положительное целое, например 15): ", 1, 2147483647);
        taken = id != oldId && list.find(id) != nullptr;
        if (taken) cout << "Этот ID уже занят.\n";
    } while (taken);
    return id;
}

int readStatus(Record& r, const string& currentDate) {
    const int CorrectDate = 5;
    int status;
    do {
        const char* labels[6]{};
        for (int i = Unknown; i <= Cancelled; ++i)
            if (allowsStatus(r, i, currentDate)) labels[i] = statuses[i];
        if (!labels[Unknown] && r.rating < 0) {
            cout << "Плановая дата прошла. Подтвердите выход или уточните дату.\n";
            labels[CorrectDate] = "уточнить дату премьеры";
        }
        cout << "Статус:\n";
        status = choose("Выбор статуса: ", labels, 6);
        if (status == CorrectDate) r.premiere = readDate(datePrompt);
    } while (status == CorrectDate);
    return status;
}

// добавление, поиск и редактирование
void print(const Record& r) {
    cout << "\nID: " << r.id << " | " << types[r.type] << " | " << r.title << "\nIMDb: ";
    if (r.rating >= 0) cout << r.rating;
    else cout << missingValue;
    cout << " | Жанр: " << (r.genre.empty() ? missingValue : r.genre)
         << (r.status != Released && !r.premiere.empty() ? "\nПлановая премьера: " : "\nПремьера: ")
         << (r.premiere.empty() ? missingValue : r.premiere) << " | Длительность: ";
    if (r.minutes) cout << r.minutes << " мин.";
    else cout << missingValue;
    cout << "\nРежиссёр: " << (r.director.empty() ? missingValue : r.director)
         << " | Возраст: " << ages[r.age] << " | Статус: " << statuses[r.status] << '\n';
}

void add(List& list, const string& currentDate) {
    cout << "\nДобавление записи. Вводите каждое поле отдельной строкой.\n";
    Record r;
    r.id = readId(list);
    r.type = readInt(typePrompt, 1, 3);
    r.title = ask("Название (обязательно, например Начало): ", true);
    r.genre = readOptionalText("Жанр (например Фантастика, 0 — нет данных): ");
    r.premiere = readDate(datePrompt);
    r.minutes = readInt("Длительность в целых минутах (например 148, 0 — нет данных): ", 0, 2147483647);
    r.director = readOptionalText("Режиссёр (например Кристофер Нолан, 0 — нет данных): ");
    r.age = readInt(agePrompt, 0, 5);
    r.status = readStatus(r, currentDate);
    if (r.status == Released)
        r.rating = readRating("IMDb от 0 до 10 (например 8,8, 0 — нет оценки, 0,0 — нулевая оценка): ");
    else cout << "Оценку IMDb можно указать после выхода.\n";
    list.append(r);
    cout << "Запись добавлена.\n";
}

Node* selectRecord(const List& list, const char* prompt) {
    const string question = string(prompt) + " (0 — отмена): ";
    Node* node;
    do {
        const int id = readInt(question, 0, 2147483647);
        if (id == 0) return nullptr;
        node = list.find(id);
        if (!node) cout << "Запись не найдена. Проверьте ID и попробуйте снова.\n";
    } while (!node);
    return node;
}

bool removeRecord(List& list) {
    Node* node = selectRecord(list, "ID удаляемой записи");
    if (!node) return false;
    list.erase(node);
    cout << "Запись удалена.\n";
    return true;
}

void search(const List& list) {
    bool hasDirector = false;
    for (const Node* p = list.first; p && !hasDirector; p = p->next)
        hasDirector = !trim(p->record.director).empty();
    const char* labels[] = {"вернуться в меню", "ID", "название", hasDirector ? "режиссёр" : nullptr};
    const int mode = choose("Выбор поиска: ", labels, 4);
    if (mode == 0) return;
    if (mode == 1) {
        if (const Node* p = selectRecord(list, "ID записи")) print(p->record);
        return;
    }
    do {
        const string query = lower(ask(mode == 2 ? "Название или его часть (например Начало): "
                                                 : "Имя или его часть (например Нолан): ", true));
        int found = 0;
        for (const Node* p = list.first; p; p = p->next) {
            const string& field = mode == 2 ? p->record.title : p->record.director;
            if (lower(field).find(query) != string::npos) {
                print(p->record);
                ++found;
            }
        }
        if (found) {
            cout << "Найдено: " << found << '\n';
            return;
        }
        cout << "Ничего не найдено. Проверьте запрос.\n";
    } while (readInt(retryPrompt, 0, 1) == 1);
}

bool edit(List& list, const string& currentDate) {
    Node* node = selectRecord(list, "ID редактируемой записи");
    if (!node) return false;
    Record r = node->record;  // черновик, который можно отменить без изменения записи в узле
    int field;
    do {
        print(r);
        const bool released = r.status == Released;
        const bool statusValid = allowsStatus(r, r.status, currentDate);
        const char* labels[] = {
            statusValid ? "сохранить изменения" : nullptr, "ID", "тип", "название", released ? "IMDb" : nullptr, "жанр",
            r.status != Cancelled ? "премьера" : nullptr, "длительность", "режиссёр", "возраст",
            r.rating < 0 ? (released ? "исправить статус выхода" : "статус") : nullptr, "отменить"
        };
        if (!statusValid) cout << reviewMessage;
        if (r.rating >= 0) cout << "Чтобы исправить статус, сначала удалите оценку IMDb.\n";
        cout << "\nИзменить:\n";
        field = choose("Выбор поля: ", labels, 12);
        switch (field) {
        case 0:
        case 11: break;
        case 1: r.id = readId(list, node->record.id); break;
        case 2: r.type = readInt(typePrompt, 1, 3); break;
        case 3: r.title = ask("Новое название (обязательно, например Начало): ", true); break;
        case 4: r.rating = readRating("IMDb от 0 до 10 (0 — удалить, 0,0 — нулевая оценка): "); break;
        case 5: r.genre = readOptionalText("Жанр (например Фантастика, 0 — удалить): "); break;
        case 6: {
            const string max = r.status == Released ? currentDate : "";
            string prompt = "Премьера ДД-ММ-ГГГГ";
            if (!max.empty()) {
                cout << "Чтобы указать будущую дату, сначала исправьте статус «Вышел».\n";
                prompt += " (не позже " + max + ")";
            }
            r.premiere = readDate(prompt + " (например 16-07-2010, 0 — удалить): ", max, true);
            break;
        }
        case 7: r.minutes = readInt("Длительность в целых минутах (например 148, 0 — удалить): ", 0, 2147483647); break;
        case 8: r.director = readOptionalText("Режиссёр (например Кристофер Нолан, 0 — удалить): "); break;
        case 9: r.age = readInt(agePrompt, 0, 5); break;
        case 10: r.status = readStatus(r, currentDate); break;
        }
    } while (field != 0 && field != 11);

    if (field == 11) {
        cout << "Изменения отменены.\n";
        return false;
    }
    if (sameRecord(node->record, r)) {
        cout << "Изменений нет.\n";
        return false;
    }
    string titleKey = titleSortKey(r.title);
    swap(node->record, r);
    swap(node->titleKey, titleKey);
    cout << "Изменения сохранены.\n";
    return true;
}

// сортировка и фильтрация обходом списка
enum SortField { AnyField, TitleField, PremiereField, RatingField, MinutesField };
const char* sortFields[] = {"", "название", "премьера", "IMDb", "длительность"};

bool hasField(const Record& r, SortField field) {
    if (field == PremiereField) return !r.premiere.empty();
    if (field == RatingField) return r.rating >= 0;
    if (field == MinutesField) return r.minutes > 0;
    return true;
}

bool comesAfter(const Node& a, const Node& b, SortField field, bool ascending) {
    // неизвестные значения всегда в конце, а равные записи сохраняют порядок
    if (!hasField(b.record, field)) return false;
    if (!hasField(a.record, field)) return true;
    const Record& left = ascending ? a.record : b.record;
    const Record& right = ascending ? b.record : a.record;
    if (field == PremiereField) return compareDates(left.premiere, right.premiere) > 0;
    if (field == RatingField) return left.rating > right.rating;
    if (field == MinutesField) return left.minutes > right.minutes;
    return ascending ? a.titleKey > b.titleKey : b.titleKey > a.titleKey;
}

Node* mergeSort(Node* first, int count, SortField field, bool ascending, bool& changed) {
    if (count < 2) return first;
    // разрываем список посередине, сортируем половины и восстанавливаем обе ссылки при слиянии
    const int half = count / 2;
    Node* right = first;
    for (int i = 0; i < half; ++i) right = right->next;
    right->prev->next = nullptr;
    right->prev = nullptr;
    Node* left = mergeSort(first, half, field, ascending, changed);
    right = mergeSort(right, count - half, field, ascending, changed);

    Node* head = nullptr;
    Node* tail = nullptr;
    while (left || right) {
        const bool fromRight = right && (!left || comesAfter(*left, *right, field, ascending));
        if (fromRight && left) changed = true;
        Node*& part = fromRight ? right : left;
        Node* node = part;
        part = part->next;
        node->prev = tail;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
    }
    tail->next = nullptr;
    return head;
}

struct Filter {
    int type = 0, age = -1, status = -1;
    string genre, fromDate, toDate;
    double fromRating = -1, toRating = -1;

    bool matches(const Record& r) const {
        if (type && type != r.type) return false;
        if (age >= 0 && age != r.age) return false;
        if (status >= 0 && status != r.status) return false;
        if (!genre.empty() && genre != lower(trim(r.genre))) return false;
        // пустая дата меньше любой нижней границы, а при верхней границе отсеиваем её явно
        if (!fromDate.empty() && compareDates(r.premiere, fromDate) < 0) return false;
        if (!toDate.empty() && (r.premiere.empty() || compareDates(r.premiere, toDate) > 0)) return false;
        // оценка -1 означает отсутствие данных и допустима только без обеих границ
        return r.rating >= fromRating && (toRating < 0 || (r.rating >= 0 && r.rating <= toRating));
    }
};

bool hasMatch(const List& list, const Filter& filter, SortField field = AnyField) {
    for (const Node* p = list.first; p; p = p->next)
        if (filter.matches(p->record) && hasField(p->record, field)) return true;
    return false;
}

int printRecords(const List& list, const Filter* filter) {
    // фильтр управляет выводом, а узлы списка остаются на месте
    int printed = 0;
    for (const Node* p = list.first; p; p = p->next) {
        if (!filter || filter->matches(p->record)) {
            print(p->record);
            ++printed;
        }
    }
    return printed;
}

bool sortCatalog(List& list) {
    const char* labels[5] = {"назад"};
    for (int i = TitleField; i <= MinutesField; ++i)
        if (hasMatch(list, {}, static_cast<SortField>(i))) labels[i] = sortFields[i];
    cout << "Поле сортировки:\n";
    const SortField field = static_cast<SortField>(choose("Выбор поля: ", labels, 5));
    if (field == AnyField) return false;
    const int order = readInt("Порядок (1 — возрастание, 2 — убывание, 0 — назад): ", 0, 2);
    if (order == 0) return false;
    bool changed = false;
    list.first = mergeSort(list.first, list.count, field, order == 1, changed);
    list.last = list.first;
    while (list.last && list.last->next) list.last = list.last->next;
    cout << "Каталог отсортирован.\n";
    printRecords(list);
    return changed;
}

enum Category { ByType, ByAge, ByStatus };
int filterOption(const List& list, const Filter& filter, Category category) {
    const char* captions[] = {"Тип", "Возраст", "Статус"};
    const char* const* values[] = {&types[1], ages, statuses};
    const int counts[] = {4, 7, 6};
    const int count = counts[category];
    const char* labels[7] = {"любой"};
    int options = 0, only = 0;
    for (const Node* p = list.first; p; p = p->next) {
        const Record& r = p->record;
        if (!filter.matches(r)) continue;
        const int index = category == ByType ? r.type : (category == ByAge ? r.age + 1 : r.status + 1);
        if (labels[index]) continue;
        labels[index] = values[category][index - 1];
        ++options;
        only = index;
    }
    if (options == 1) {
        cout << captions[category] << ": " << labels[only] << '\n';
        return only;
    }
    cout << captions[category] << ":\n";
    return choose("Выбор условия: ", labels, count);
}

Filter readFilter(const List& list) {
    Filter f;
    cout << "Условия объединяются по следующим признакам. Введите 0, чтобы пропустить условие.\n";
    f.type = filterOption(list, f, ByType);
    f.genre = lower(readOptionalText("Жанр целиком (например Драма, 0 — любой): "));
    if (!hasMatch(list, f)) return f;

    f.age = filterOption(list, f, ByAge) - 1;
    if (hasMatch(list, f, PremiereField)) {
        f.fromDate = readDate("Премьера от ДД-ММ-ГГГГ (например 01-01-2020, 0 — без границы): ");
        f.toDate = readDate("Премьера до ДД-ММ-ГГГГ (0 — без границы): ", f.fromDate);
    }
    if (!hasMatch(list, f)) return f;

    f.status = filterOption(list, f, ByStatus) - 1;
    if (hasMatch(list, f, RatingField)) {
        f.fromRating = readRating("IMDb от (0 — без границы, 0,0 — нулевая оценка): ");
        f.toRating = readRating("IMDb до (0 — без границы, 0,0 — нулевая оценка): ",
                                f.fromRating < 0 ? 0 : f.fromRating);
    }
    return f;
}

void filterCatalog(const List& list) {
    do {
        const Filter filter = readFilter(list);
        const int found = printRecords(list, &filter);
        if (found) {
            cout << "Отобрано: " << found << " из " << list.count << '\n';
            return;
        }
        cout << "По заданным условиям ничего не найдено.\n";
    } while (readInt(retryPrompt, 0, 1) == 1);
}

// проверка пути, сохранение и загрузка текстового файла
bool validFileName(const string& name) {
    if (name.empty() || name.back() == '.' || name.back() == ' '
        || !validText(name) || name.find_first_of("<>:\"/\\|?*\t") != string::npos) return false;
    const string stem = lower(trim(name.substr(0, name.find('.'))));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    return !(stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt")
             && stem[3] >= '1' && stem[3] <= '9');
}

bool checkFile(const fs::path& path, bool saving, bool& exists) {
    error_code error;
    const fs::file_status info = fs::symlink_status(path, error);
    if (info.type() == fs::file_type::not_found) {
        exists = false;
        if (saving) {
            const fs::path folder = path.has_parent_path() ? path.parent_path() : fs::path(".");
            if (fs::is_directory(folder, error) && !error) return true;
            cout << "Папка для сохранения не найдена или недоступна.\n";
        } else cout << "Файл не найден.\n";
        return false;
    }
    exists = true;
    if (!error && fs::is_regular_file(info)) return true;
    cout << "Нужен доступный обычный файл. Папки и символические ссылки не подходят.\n";
    return false;
}

fs::path chooseFile(bool saving, bool& exists) {
    fs::path path;
    bool ready;
    do {
        const string name = ask("Имя или путь файла (Enter — media_catalog.txt, 0 — отмена): ", false, false);
        if (name == "0") return {};
        bool valid;
        try {
            path = fs::u8path(name.empty() ? "media_catalog.txt" : name);
            valid = validFileName(path.filename().u8string());
        } catch (const fs::filesystem_error&) {
            valid = false;
        }
        if (!valid) cout << "Недопустимое имя файла. Укажите обычное имя, например catalog.txt.\n";
        ready = valid && checkFile(path, saving, exists);
    } while (!ready);
    return path;
}

enum FileField {
    FileId, FileType, FileTitle, FileRating, FileGenre, FileDate,
    FileMinutes, FileDirector, FileAge, FileStatus, FileFieldCount
};
const char* const fileLabels[FileFieldCount] = {
    "ID:", "Тип:", "Название:", "Оценка IMDb:", "Жанр:", "Дата премьеры:",
    "Длительность (мин):", "Режиссёр:", "Возрастное ограничение:", "Статус выхода:"
};
const string emptyCatalog = "Каталог пуст.";

template<class Value>
void writeField(ostream& out, FileField field, const Value& value, bool present = true) {
    out << fileLabels[field] << ' ';
    if (present) out << value;
    else out << missingValue;
    out << '\n';
}

void writeRecord(ostream& out, const Record& r) {
    // кавычки различают текст «нет данных» и отметку об отсутствующем поле
    writeField(out, FileId, r.id);
    writeField(out, FileType, types[r.type]);
    writeField(out, FileTitle, quoted(r.title), !r.title.empty());
    writeField(out, FileRating, r.rating, r.rating >= 0);
    writeField(out, FileGenre, quoted(r.genre), !r.genre.empty());
    writeField(out, FileDate, r.premiere, !r.premiere.empty());
    writeField(out, FileMinutes, r.minutes, r.minutes > 0);
    writeField(out, FileDirector, quoted(r.director), !r.director.empty());
    writeField(out, FileAge, ages[r.age]);
    writeField(out, FileStatus, statuses[r.status]);
}

fs::path createStagingDirectory(const fs::path& target) {
    const string targetName = lower(target.filename().u8string());
    for (int i = 0; i < 100; ++i) {
        string name = ".media_catalog_saving";
        if (i > 0) name += "_" + to_string(i);
        if (name == targetName) continue;
        const fs::path candidate = target.parent_path() / name;
        error_code error;
        if (fs::create_directory(candidate, error)) return candidate;
        if (error && error != errc::file_exists) break;
    }
    return {};
}

bool saveFile(const List& list, const fs::path& path, bool exists) {
    // сначала пишем рядом с каталогом, затем заменяем файл после успешной записи
    const fs::path staging = createStagingDirectory(path);
    if (staging.empty()) {
        cout << "Не удалось создать временную папку рядом с файлом.\n";
        return false;
    }
    error_code error;
    const fs::path temp = staging / "catalog.tmp";
    ofstream out(temp);
    out << fixed << setprecision(1);
    if (!list.first) out << emptyCatalog << '\n';
    for (const Node* p = list.first; p; p = p->next) {
        writeRecord(out, p->record);
        if (p->next) out << '\n';
    }
    out.close();
    // если после выбора пути появился новый файл, разрешение на перезапись не получено
    bool nowExists;
    bool saved = out && checkFile(path, true, nowExists) && (exists || !nowExists);
    if (saved) {
        fs::rename(temp, path, error);
        saved = !error;
    }
    fs::remove(temp, error);
    fs::remove(staging, error);
    if (saved) cout << "Каталог сохранён.\n";
    else cout << "Ошибка записи. Укажите путь ещё раз или выберите другой файл.\n";
    return saved;
}

bool save(const List& list) {
    const string currentDate = today();
    if (currentDate.empty()) {
        cout << "Не удалось определить текущую дату. Каталог не сохранён.\n";
        return false;
    }
    for (const Node* node = list.first; node; node = node->next) {
        if (!allowsStatus(node->record, node->record.status, currentDate)) {
            cout << "Каталог не сохранён. Проверьте запись с ID " << node->record.id << ". " << reviewMessage;
            return false;
        }
    }
    bool exists;
    fs::path path;
    do {
        path = chooseFile(true, exists);
        if (path.empty()) return false;
        if (exists && readInt("Файл уже существует (1 — перезаписать, 0 — отмена): ", 0, 1) == 0)
            return false;
    } while (!saveFile(list, path, exists));
    return true;
}

bool readFileText(const string& text, string& value) {
    if (text == missingValue) {
        value.clear();
        return true;
    }
    istringstream in(text);
    if (in.peek() != '"' || !(in >> quoted(value))) return false;
    in >> ws;
    return in.eof() && validText(value);
}

int findLabel(const string& value, const char* const labels[], int count) {
    for (int i = 0; i < count; ++i)
        if (value == labels[i]) return i;
    return -1;
}

bool readRecord(istream& in, string line, Record& r, const string& currentDate, LineResult& result) {
    string values[FileFieldCount];
    for (int i = 0; i < FileFieldCount; ++i) {
        if (i != FileId) {
            result = readLine(in, line, FileLineLimit);
            if (result != LineOk) return false;
        }
        line = trim(line);
        const string label = fileLabels[i];
        if (line.compare(0, label.size(), label) != 0) return false;
        values[i] = trim(line.substr(label.size()));
    }
    if (!parseNumber(values[FileId], r.id) || r.id <= 0
        || !readFileText(values[FileTitle], r.title) || trim(r.title).empty()
        || !readFileText(values[FileGenre], r.genre)
        || !readFileText(values[FileDirector], r.director))
        return false;
    r.type = findLabel(values[FileType], types, 4);
    r.age = findLabel(values[FileAge], ages, 6);
    r.status = findLabel(values[FileStatus], statuses, 5);
    if (r.type <= 0 || r.age < 0) return false;
    if (values[FileRating] != missingValue && !parseRatingNumber(values[FileRating], r.rating)) return false;
    if (values[FileMinutes] != missingValue &&
        (!parseNumber(values[FileMinutes], r.minutes) || r.minutes <= 0)) return false;
    if (values[FileDate] != missingValue) {
        if (!validDate(values[FileDate])) return false;
        r.premiere = values[FileDate];
    }
    return allowsStatus(r, r.status, currentDate, true);
}

bool loadFile(List& list, const fs::path& path, const string& currentDate) {
    ifstream in(path);
    if (!in) { cout << "Не удалось открыть файл.\n"; return false; }
    string line;

    List loaded;  // текущий список заменяется только после чтения всех строк
    unordered_set<int> ids;  // используется только при проверке уникальности во время загрузки
    bool markedEmpty = false;  // после отметки о пустом каталоге допустимы только пустые строки
    size_t number = 0;
    int overdue = 0;
    LineResult result = readLine(in, line, FileLineLimit);
    for (; result == LineOk; result = readLine(in, line, FileLineLimit)) {
        ++number;
        if (number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = trim(line);
        if (line.empty()) continue;
        if (line == emptyCatalog && !loaded.first && !markedEmpty) {
            markedEmpty = true;
            continue;
        }
        Record r;
        if (markedEmpty || !readRecord(in, line, r, currentDate, result) || !ids.insert(r.id).second) {
            if (result == LineTooLong || result == LineError) break;
            cout << "Строка " << number << ": неверные данные или повторяющийся ID. Каталог не изменён.\n";
            return false;
        }
        number += FileFieldCount - 1;
        if (!allowsStatus(r, r.status, currentDate)) ++overdue;
        loaded.append(r);
    }
    if (result == LineTooLong) {
        cout << "Слишком длинная строка в файле. Максимум " << FileLineLimit << " байт. Каталог не изменён.\n";
        return false;
    }
    if (result == LineError) { cout << "Ошибка чтения. Каталог не изменён.\n"; return false; }
    if (!loaded.first && !markedEmpty) {
        cout << "Файл пуст. Каталог не изменён.\n";
        return false;
    }
    // бывшие записи освободит деструктор временного списка
    swap(list.first, loaded.first);
    swap(list.last, loaded.last);
    swap(list.count, loaded.count);
    cout << "Каталог загружен.\n";
    if (overdue) cout << "Записей с просроченной плановой датой: " << overdue << ". " << reviewMessage;
    return true;
}

bool load(List& list, const string& currentDate) {
    bool exists;
    bool loaded = false;
    do {
        const fs::path path = chooseFile(false, exists);
        if (path.empty()) return false;
        try {
            loaded = loadFile(list, path, currentDate);
        } catch (const bad_alloc&) {
            cout << "Не хватает памяти для загрузки. Каталог не изменён.\n";
        }
    } while (!loaded);
    return true;
}

bool confirmReplace(const List& list, bool& dirty) {
    if (!dirty) return true;
    const int action = readInt(
        "Есть несохранённые изменения (1 — сохранить, 2 — продолжить без сохранения, 0 — отменить): ", 0, 2);
    if (action == 2) return true;
    if (action == 1 && save(list)) {
        dirty = false;
        return true;
    }
    cout << "Действие отменено. Каталог не изменён.\n";
    return false;
}

// главное меню
void pauseMenu() {
    string input;
    do {
        input = ask("\nНажмите Enter, чтобы вернуться в меню: ", false, false);
        if (!input.empty()) cout << "Оставьте строку пустой.\n";
    } while (!input.empty());
#ifdef _WIN32
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(output, &mode) ||
        !SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return;
#else
    if (!isatty(STDOUT_FILENO)) return;
#endif
    // очищаем экран и историю прокрутки, если терминал это поддерживает
    cout << "\x1b[2J\x1b[3J\x1b[H" << flush;
}

const char* recordWord(int count) {
    const int lastTwo = count % 100;
    if (lastTwo >= 11 && lastTwo <= 14) return "записей";
    const int last = count % 10;
    if (last == 1) return "запись";
    if (last >= 2 && last <= 4) return "записи";
    return "записей";
}

int readMenu(int count) {
    const char* labels[] = {"Выход", "Добавить", "Найти", "Редактировать", "Удалить по ID",
                            "Показать всё", "Сортировать", "Сохранить в файл", "Загрузить файл",
                            "Фильтровать"};
    cout << "Каталог фильмов, сериалов и короткометражек\n\n"
         << "Каталог: " << count << ' ' << recordWord(count) << '\n';
    for (int i = 0; i <= 9; ++i) cout << i << " — " << labels[i] << '\n';
    cout << "Выбор: " << flush;
    string input;
    const LineResult result = readLine(cin, input, ConsoleLineLimit, true);
    if (result == LineEnd) throw EndOfInput{};
    if (result == LineError) throw runtime_error("сбой чтения из консоли");
    if (result == LineTooLong || input.size() != 1 || input[0] < '0' || input[0] > '9') return -1;
    return input[0] - '0';
}
