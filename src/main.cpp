#include "mainwindow.h"
#include "uml_class.h"
#include "uml_diagram.h"
#include "uml_relationship.h"

#include <QApplication>
#include <iostream>

int main(int argc, char *argv[])
{
    UmlClass person("Person");
    person.addAttribute("name");
    person.addAttribute("age");
    person.addMethod("speak()");
    person.addMethod("walk()");

    UmlClass student("Student");
    student.addAttribute("studentId");
    student.addMethod("study()");

    UmlDiagram diagram;

    diagram.addClass(person);
    diagram.addClass(student);

    // Student наследуется от Person
    diagram.addRelationship(
        UmlRelationship(
            "Student",
            "Person",
            RelationshipType::Inheritance
            )
        );

    std::cout << "UML Diagram\n";
    std::cout << "===========\n";

    std::cout << "\nClasses:\n";

    for (const UmlClass& umlClass : diagram.classes())
    {
        std::cout << "  " << umlClass.name() << '\n';
    }

    std::cout << "\nRelationships:\n";

    for (const UmlRelationship& relationship : diagram.relationships())
    {
        std::cout << "  " << relationship.source()
        << " -> " << relationship.target();

        if (relationship.type() == RelationshipType::Inheritance)
        {
            std::cout << " (inheritance)";
        }

        std::cout << '\n';
    }

    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}
