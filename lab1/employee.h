#ifndef EMPLOYEE_H
#define EMPLOYEE_H

struct Employee {
    int num;            // employee identification number
    char name[10];      // employee name (max 9 characters + null)
    double hours;       // worked hours
};

#endif // EMPLOYEE_H