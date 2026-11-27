#include "catalog.h"

#include <exception>
#include <iostream>

using namespace std;

int main() {
    List list;
    bool dirty = false;  // изменения в каталоге ещё нужно сохранить
    try {
        bool finished = false;  // выход возможен после обработки несохранённых изменений
        do {
            const int count = list.count;
            const int command = readMenu(count);
            if (command < 0) {
                cout << "Введите одну цифру от 0 до 9.\n";
            } else if (command == 0) {
                finished = confirmReplace(list, dirty);
            } else if (count == 0 && ((command >= 2 && command <= 6) || command == 9)) {
                cout << "Каталог пуст. Сначала добавьте или загрузите записи.\n";
            } else if (command == 6 && count == 1) {
                cout << "Для сортировки нужно минимум две записи.\n";
            } else {
                const string currentDate = today();
                if (currentDate.empty()) { cerr << "Не удалось определить текущую дату.\n"; return 1; }
                switch (command) {
                case 1: add(list, currentDate); dirty = true; break;
                case 2: search(list); break;
                case 3: if (edit(list, currentDate)) dirty = true; break;
                case 4: if (removeRecord(list)) dirty = true; break;
                case 5: printRecords(list); break;
                case 6: if (sortCatalog(list)) dirty = true; break;
                case 7: if (save(list)) dirty = false; break;
                case 8:
                    if (confirmReplace(list, dirty) && load(list, currentDate)) dirty = false;
                    break;
                case 9: filterCatalog(list); break;
                }
            }
            if (!finished) pauseMenu();
        } while (!finished);
    } catch (const EndOfInput&) {
        cout << "\nВвод завершён.\n";
        if (dirty) {
            cerr << "Ввод прерван. Несохранённые изменения в файл не записаны.\n";
            return 1;
        }
    } catch (const exception& error) {
        cerr << "Ошибка: " << error.what() << '\n';
        if (dirty) cerr << "Несохранённые изменения в файл не записаны.\n";
        return 1;
    }
}
