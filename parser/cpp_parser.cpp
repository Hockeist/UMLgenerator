#include "cpp_parser.h"

#include "../model/uml_class.h"
#include "../model/uml_relationship.h"

#include <clang-c/Index.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// -------------------------------------------------------------------------
// БЛОК 1. Вспомогательные функции строк и определения типов связей / кратности
// -------------------------------------------------------------------------

std::string trim(const std::string& str)
{
    const std::size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    const std::size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool endsWith(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(),
                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

// Очищает тип от лишних префиксов пространства имен (например, "SmartHome::Room *" -> "Room*")
// и убирает std:: для компактности, как на эталонной UML-диаграмме.
std::string cleanTypeDisplay(std::string typeStr)
{
    // Убираем std::__1:: и std::
    std::regex stdRegex(R"(\bstd::(__1::)?)");
    typeStr = std::regex_replace(typeStr, stdRegex, "");

    // Оставляем короткое имя у пользовательских пространств имен (Name::Class -> Class)
    std::regex nsRegex(R"(\b[A-Za-z_][A-Za-z0-9_]*::)");
    typeStr = std::regex_replace(typeStr, nsRegex, "");

    return trim(typeStr);
}

bool isIdentifierCharacter(char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) || ch == '_';
}

bool containsIdentifier(const std::string& text, const std::string& identifier)
{
    if (identifier.empty())
        return false;

    std::size_t position = text.find(identifier);
    while (position != std::string::npos)
    {
        const std::size_t end = position + identifier.size();
        const bool validLeft = (position == 0) || !isIdentifierCharacter(text[position - 1]);
        const bool validRight = (end >= text.size()) || !isIdentifierCharacter(text[end]);

        if (validLeft && validRight)
            return true;

        position = text.find(identifier, position + 1);
    }
    return false;
}

// Проверяет, является ли тип коллекцией/контейнером (для Агрегации и кратности "0..*")
bool isCollectionType(const std::string& typeSpelling)
{
    const char* collectionMarkers[] = {
        "vector", "list", "deque", "set", "multiset", "map", "multimap",
        "unordered_set", "unordered_map", "array", "QList", "QVector",
        "List", "IList", "IEnumerable", "ICollection", "HashSet", "Dictionary"
    };

    for (const char* marker : collectionMarkers)
    {
        if (containsIdentifier(typeSpelling, marker))
            return true;
    }

    // Массивы C# / C++ вида Room[]
    if (typeSpelling.find("[]") != std::string::npos)
        return true;

    return false;
}

// Определяет тип UML-связи для поля класса
RelationshipType relationshipTypeForField(const std::string& typeSpelling)
{
    if (containsIdentifier(typeSpelling, "unique_ptr"))
        return RelationshipType::Composition;

    if (containsIdentifier(typeSpelling, "shared_ptr") ||
        containsIdentifier(typeSpelling, "weak_ptr") ||
        isCollectionType(typeSpelling))
    {
        return RelationshipType::Aggregation;
    }

    if (typeSpelling.find('*') != std::string::npos ||
        typeSpelling.find('&') != std::string::npos)
    {
        return RelationshipType::Association;
    }

    return RelationshipType::Composition;
}

// Определяет кратность (множественность) на конце стрелки: "1", "0..1" или "0..*"
void determineMultiplicity(
    const std::string& typeSpelling,
    RelationshipType relType,
    std::string& outSourceMult,
    std::string& outTargetMult)
{
    if (relType == RelationshipType::Inheritance)
    {
        outSourceMult = "";
        outTargetMult = "";
        return;
    }

    outSourceMult = "1";

    if (relType == RelationshipType::Dependency)
    {
        outTargetMult = "";
        return;
    }

    if (isCollectionType(typeSpelling))
    {
        outTargetMult = "0..*";
    }
    else if (typeSpelling.find('*') != std::string::npos ||
             containsIdentifier(typeSpelling, "shared_ptr") ||
             containsIdentifier(typeSpelling, "weak_ptr"))
    {
        outTargetMult = "0..1";
    }
    else
    {
        outTargetMult = "1";
    }
}

struct ClassContext
{
    UmlDiagram* diagram = nullptr;
    UmlClass* currentClass = nullptr;
    std::set<std::string> knownClasses; // Короткие имена классов (без namespace!)
    std::set<std::string> relationships;
};

std::string relationshipKey(
    const std::string& source,
    const std::string& target,
    RelationshipType type)
{
    return source + "|" + target + "|" + std::to_string(static_cast<int>(type));
}

bool hasStrongRelationship(
    const ClassContext& context,
    const std::string& source,
    const std::string& target)
{
    const RelationshipType strongTypes[] = {
        RelationshipType::Inheritance,
        RelationshipType::Composition,
        RelationshipType::Aggregation,
        RelationshipType::Association
    };

    for (RelationshipType t : strongTypes)
    {
        if (context.relationships.count(relationshipKey(source, target, t)) > 0)
            return true;
    }
    return false;
}

void addRelationshipOnce(
    ClassContext& context,
    const std::string& source,
    const std::string& target,
    RelationshipType type,
    const std::string& sourceMult = "",
    const std::string& targetMult = "")
{
    if (!context.diagram || source.empty() || target.empty() || source == target)
        return;

    if (type == RelationshipType::Dependency &&
        hasStrongRelationship(context, source, target))
    {
        return;
    }

    if (!context.relationships.insert(relationshipKey(source, target, type)).second)
        return;

    context.diagram->addRelationship(
        UmlRelationship(source, target, type, sourceMult, targetMult));
}

std::vector<std::string> collectReferencedClasses(
    const std::string& typeSpelling,
    const ClassContext& context)
{
    std::vector<std::string> result;
    for (const std::string& className : context.knownClasses)
    {
        if (containsIdentifier(typeSpelling, className))
            result.push_back(className);
    }
    return result;
}

// -------------------------------------------------------------------------
// БЛОК 2. Парсер C++ на базе libclang (Формат UML: "имя : Тип", короткие имена)
// -------------------------------------------------------------------------

std::string toString(CXString value)
{
    const char* text = clang_getCString(value);
    std::string result = text ? text : "";
    clang_disposeString(value);
    return result;
}

std::string cursorSpelling(CXCursor cursor)
{
    return toString(clang_getCursorSpelling(cursor));
}

std::string cursorTypeSpelling(CXType type)
{
    return toString(clang_getTypeSpelling(type));
}

bool isClassCursor(CXCursor cursor)
{
    const CXCursorKind kind = clang_getCursorKind(cursor);
    return kind == CXCursor_ClassDecl ||
           kind == CXCursor_StructDecl ||
           kind == CXCursor_ClassTemplate;
}

bool isFromMainFile(CXCursor cursor)
{
    return clang_Location_isFromMainFile(clang_getCursorLocation(cursor)) != 0;
}

UmlAccess accessFromCursor(CXCursor cursor)
{
    switch (clang_getCXXAccessSpecifier(cursor))
    {
    case CX_CXXPublic:    return UmlAccess::Public;
    case CX_CXXProtected: return UmlAccess::Protected;
    case CX_CXXPrivate:   return UmlAccess::Private;
    default:              break;
    }

    CXCursor parent = clang_getCursorSemanticParent(cursor);
    const CXCursorKind parentKind = clang_getCursorKind(parent);

    if (parentKind == CXCursor_StructDecl)
        return UmlAccess::Public;

    return UmlAccess::Private;
}

struct ClassCollector
{
    UmlDiagram* diagram = nullptr;
    std::set<std::string> knownClasses;
};

// Первый проход C++: собираем короткие имена классов (без SmartHome::)
CXChildVisitResult collectClasses(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    auto* collector = static_cast<ClassCollector*>(clientData);

    if (!isClassCursor(cursor))
        return CXChildVisit_Recurse;

    if (!isFromMainFile(cursor) || !clang_isCursorDefinition(cursor))
        return CXChildVisit_Continue;

    // Берем чистое короткое имя класса (например, "Device" вместо "SmartHome::Device")
    const std::string shortClassName = cursorSpelling(cursor);
    if (shortClassName.empty())
        return CXChildVisit_Continue;

    collector->knownClasses.insert(shortClassName);

    if (!collector->diagram->findClass(shortClassName))
        collector->diagram->addClass(UmlClass(shortClassName));

    return CXChildVisit_Recurse;
}

struct ParameterCollector
{
    std::vector<std::string> parameters;
    std::vector<std::string> parameterTypes;
};

// Форматируем параметры методов также в стиле UML: "имя : Тип"
CXChildVisitResult collectParameters(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    auto* collector = static_cast<ParameterCollector*>(clientData);

    if (clang_getCursorKind(cursor) != CXCursor_ParmDecl)
        return CXChildVisit_Continue;

    const std::string rawType = cursorTypeSpelling(clang_getCursorType(cursor));
    const std::string cleanType = cleanTypeDisplay(rawType);
    const std::string paramName = cursorSpelling(cursor);

    if (!rawType.empty())
        collector->parameterTypes.push_back(rawType);

    if (!paramName.empty() && !cleanType.empty())
        collector->parameters.push_back(paramName + ": " + cleanType);
    else if (!cleanType.empty())
        collector->parameters.push_back(cleanType);

    return CXChildVisit_Continue;
}

// Форматируем метод в классическом стиле UML: "имя(парам: Тип): ВозвращаемыйТип"
std::string formatMethodAndCollectTypes(
    CXCursor cursor,
    std::vector<std::string>& usedTypes)
{
    const CXCursorKind kind = clang_getCursorKind(cursor);
    const std::string name = cursorSpelling(cursor);

    if (name.empty())
        return {};

    std::string result = name + "(";

    ParameterCollector parameters;
    clang_visitChildren(cursor, collectParameters, &parameters);

    for (const std::string& paramType : parameters.parameterTypes)
        usedTypes.push_back(paramType);

    for (std::size_t i = 0; i < parameters.parameters.size(); ++i)
    {
        if (i > 0)
            result += ", ";
        result += parameters.parameters[i];
    }

    result += ")";

    if (kind != CXCursor_Constructor && kind != CXCursor_Destructor)
    {
        const std::string rawReturn = cursorTypeSpelling(clang_getCursorResultType(cursor));
        if (!rawReturn.empty())
        {
            usedTypes.push_back(rawReturn);
            const std::string cleanRet = cleanTypeDisplay(rawReturn);
            if (!cleanRet.empty() && cleanRet != "void")
                result += ": " + cleanRet;
        }
    }

    return result;
}

struct MemberVisitorContext
{
    ClassContext* context = nullptr;
    std::vector<std::string> pendingDependencyTargets;
};

CXChildVisitResult visitClassMember(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    auto* visitorContext = static_cast<MemberVisitorContext*>(clientData);
    ClassContext& context = *visitorContext->context;

    if (!context.currentClass)
        return CXChildVisit_Continue;

    const CXCursorKind kind = clang_getCursorKind(cursor);

    // 1. Наследование
    if (kind == CXCursor_CXXBaseSpecifier)
    {
        const std::string baseType = cursorTypeSpelling(clang_getCursorType(cursor));
        for (const std::string& baseClass : collectReferencedClasses(baseType, context))
        {
            addRelationshipOnce(
                context,
                context.currentClass->name(),
                baseClass,
                RelationshipType::Inheritance);
        }
        return CXChildVisit_Continue;
    }

    // 2. Поля (атрибуты) в формате "имя : Тип"
    if (kind == CXCursor_FieldDecl || kind == CXCursor_VarDecl)
    {
        const std::string rawType = cursorTypeSpelling(clang_getCursorType(cursor));
        const std::string cleanType = cleanTypeDisplay(rawType);
        const std::string fieldName = cursorSpelling(cursor);

        if (fieldName.empty())
            return CXChildVisit_Continue;

        // Классический UML-формат: "name: Type"
        const std::string attribute =
            cleanType.empty() ? fieldName : (fieldName + ": " + cleanType);

        context.currentClass->addAttribute(attribute, accessFromCursor(cursor));

        const RelationshipType relType = relationshipTypeForField(rawType);
        std::string srcMult, tgtMult;
        determineMultiplicity(rawType, relType, srcMult, tgtMult);

        for (const std::string& target : collectReferencedClasses(rawType, context))
        {
            addRelationshipOnce(
                context,
                context.currentClass->name(),
                target,
                relType,
                srcMult,
                tgtMult);
        }

        return CXChildVisit_Continue;
    }

    // 3. Методы
    if (kind == CXCursor_CXXMethod ||
        kind == CXCursor_Constructor ||
        kind == CXCursor_Destructor ||
        kind == CXCursor_FunctionTemplate)
    {
        std::vector<std::string> usedTypes;
        const std::string method = formatMethodAndCollectTypes(cursor, usedTypes);

        if (!method.empty())
            context.currentClass->addMethod(method, accessFromCursor(cursor));

        for (const std::string& typeStr : usedTypes)
        {
            for (const std::string& target : collectReferencedClasses(typeStr, context))
                visitorContext->pendingDependencyTargets.push_back(target);
        }
    }

    return CXChildVisit_Continue;
}

struct ClassMemberCollector
{
    ClassContext* context = nullptr;
};

CXChildVisitResult visitClassDefinition(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    auto* collector = static_cast<ClassMemberCollector*>(clientData);

    if (!isClassCursor(cursor))
        return CXChildVisit_Recurse;

    if (!isFromMainFile(cursor) || !clang_isCursorDefinition(cursor))
        return CXChildVisit_Continue;

    const std::string className = cursorSpelling(cursor);
    if (className.empty())
        return CXChildVisit_Continue;

    UmlClass* umlClass = collector->context->diagram->findClassMutable(className);
    if (!umlClass)
        return CXChildVisit_Continue;

    collector->context->currentClass = umlClass;

    MemberVisitorContext memberContext;
    memberContext.context = collector->context;

    clang_visitChildren(cursor, visitClassMember, &memberContext);

    for (const std::string& target : memberContext.pendingDependencyTargets)
    {
        addRelationshipOnce(
            *collector->context,
            umlClass->name(),
            target,
            RelationshipType::Dependency);
    }

    collector->context->currentClass = nullptr;
    return CXChildVisit_Recurse;
}

// -------------------------------------------------------------------------
// БЛОК 3. Встроенный парсер исходного кода Python (.py)
// -------------------------------------------------------------------------

bool parsePythonFile(
    const std::string& filePath,
    UmlDiagram& diagram,
    std::string& errorMessage)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        errorMessage = "Не удалось открыть Python-файл: " + filePath;
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);

    ClassContext context;
    context.diagram = &diagram;

    std::regex classRegex(R"(^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\(([^)]*)\))?\s*:)");
    std::regex defRegex(R"(^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(?:->\s*([^:]+))?\s*:)");
    std::regex classAttrRegex(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^=\n#]+)(?:=\s*(.+))?)");
    std::regex selfAttrRegex(R"(^\s*self\.([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([^=\n#]+))?\s*=\s*(.+))");

    // Проход 1: Находим все классы Python
    for (const std::string& rawLine : lines)
    {
        std::smatch match;
        if (std::regex_search(rawLine, match, classRegex))
        {
            const std::string className = match[1].str();
            context.knownClasses.insert(className);
            if (!diagram.findClass(className))
                diagram.addClass(UmlClass(className));
        }
    }

    auto pyAccess = [](const std::string& memberName) -> UmlAccess
    {
        if (memberName.size() >= 2 && memberName.substr(0, 2) == "__" &&
            memberName.substr(memberName.size() - 2) != "__")
        {
            return UmlAccess::Private;
        }
        if (!memberName.empty() && memberName[0] == '_' &&
            !(memberName.size() >= 2 && memberName.substr(0, 2) == "__"))
        {
            return UmlAccess::Protected;
        }
        return UmlAccess::Public;
    };

    // Проход 2: Извлекаем наследование, поля, методы и связи в Python
    UmlClass* currentClass = nullptr;
    std::set<std::string> addedAttrs;
    std::vector<std::string> pendingDeps;

    auto flushDeps = [&]()
    {
        if (currentClass)
        {
            for (const std::string& target : pendingDeps)
            {
                addRelationshipOnce(context, currentClass->name(), target, RelationshipType::Dependency);
            }
        }
        pendingDeps.clear();
        addedAttrs.clear();
    };

    for (const std::string& rawLine : lines)
    {
        const std::string trimmed = trim(rawLine);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        std::smatch match;

        // Объявление класса
        if (std::regex_search(rawLine, match, classRegex))
        {
            flushDeps();
            const std::string className = match[1].str();
            currentClass = diagram.findClassMutable(className);
            context.currentClass = currentClass;

            const std::string bases = match[2].str();
            for (const std::string& base : collectReferencedClasses(bases, context))
            {
                addRelationshipOnce(context, className, base, RelationshipType::Inheritance);
            }
            continue;
        }

        if (!currentClass)
            continue;

        // Объявление метода (def ...)
        if (std::regex_search(rawLine, match, defRegex))
        {
            const std::string methodName = match[1].str();
            std::string params = trim(match[2].str());
            std::string retType = trim(match[3].str());

            // Убираем первый аргумент self / cls
            std::regex selfParamRegex(R"(^(self|cls)\s*(,\s*)?)");
            params = std::regex_replace(params, selfParamRegex, "");

            std::string formatted = methodName + "(" + params + ")";
            if (!retType.empty() && retType != "None")
                formatted += ": " + retType;

            currentClass->addMethod(formatted, pyAccess(methodName));

            for (const std::string& target : collectReferencedClasses(params + " " + retType, context))
                pendingDeps.push_back(target);

            continue;
        }

        // Поле внутри __init__: self.x: Type = val
        if (std::regex_search(rawLine, match, selfAttrRegex))
        {
            const std::string attrName = match[1].str();
            std::string attrType = trim(match[2].str());
            const std::string rhs = trim(match[3].str());

            if (attrType.empty())
            {
                // Если тип явно не указан через двоеточие, пробуем угадать по вызову конструктора справа
                for (const std::string& cls : context.knownClasses)
                {
                    if (containsIdentifier(rhs, cls))
                    {
                        attrType = (rhs.find('[') != std::string::npos) ? ("list[" + cls + "]") : cls;
                        break;
                    }
                }
                if (attrType.empty())
                    attrType = "Any";
            }

            if (addedAttrs.insert(attrName).second)
            {
                currentClass->addAttribute(attrName + ": " + attrType, pyAccess(attrName));
            }

            const std::string combined = attrType + " " + rhs;
            const bool isList = containsIdentifier(combined, "list") ||
                                containsIdentifier(combined, "List") ||
                                combined.find('[') != std::string::npos;

            const RelationshipType relType = isList
                                                 ? RelationshipType::Aggregation
                                                 : RelationshipType::Composition;
            const std::string tgtMult = isList ? "0..*" : "1";

            for (const std::string& target : collectReferencedClasses(combined, context))
            {
                addRelationshipOnce(context, currentClass->name(), target, relType, "1", tgtMult);
            }
            continue;
        }

        // Поле на уровне класса (dataclass / typing): name: str
        if (std::regex_search(rawLine, match, classAttrRegex))
        {
            const std::string attrName = match[1].str();
            const std::string attrType = trim(match[2].str());

            if (attrName != "return" && addedAttrs.insert(attrName).second)
            {
                currentClass->addAttribute(attrName + ": " + attrType, pyAccess(attrName));

                const bool isList = containsIdentifier(attrType, "list") ||
                                    containsIdentifier(attrType, "List");
                const RelationshipType relType = isList
                                                     ? RelationshipType::Aggregation
                                                     : RelationshipType::Composition;
                const std::string tgtMult = isList ? "0..*" : "1";

                for (const std::string& target : collectReferencedClasses(attrType, context))
                {
                    addRelationshipOnce(context, currentClass->name(), target, relType, "1", tgtMult);
                }
            }
        }
    }

    flushDeps();
    return true;
}

// -------------------------------------------------------------------------
// БЛОК 4. Встроенный парсер исходного кода C# (.cs)
// -------------------------------------------------------------------------

bool parseCSharpFile(
    const std::string& filePath,
    UmlDiagram& diagram,
    std::string& errorMessage)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        errorMessage = "Не удалось открыть C#-файл: " + filePath;
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);

    ClassContext context;
    context.diagram = &diagram;

    std::regex classRegex(R"(\b(?:class|interface|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([^{]+))?)");
    std::regex methodRegex(R"(^\s*(?:(public|protected|private|internal)\s+)?(?:(?:static|virtual|abstract|override|async)\s+)*([A-Za-z_][A-Za-z0-9_<>,\[\]\?\s]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\))");
    std::regex ctorRegex(R"(^\s*(?:(public|protected|private)\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\))");
    std::regex fieldOrPropRegex(R"(^\s*(?:(public|protected|private|internal)\s+)?(?:(?:static|readonly)\s+)*([A-Za-z_][A-Za-z0-9_<>,\[\]\?]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\{|;|=))");

    // Проход 1: Собираем все классы и интерфейсы C#
    for (const std::string& rawLine : lines)
    {
        const std::string trimmed = trim(rawLine);
        if (trimmed.substr(0, 2) == "//")
            continue;

        std::smatch match;
        if (std::regex_search(rawLine, match, classRegex))
        {
            const std::string className = match[1].str();
            context.knownClasses.insert(className);
            if (!diagram.findClass(className))
                diagram.addClass(UmlClass(className));
        }
    }

    auto csAccess = [](const std::string& spec) -> UmlAccess
    {
        if (spec == "public" || spec == "internal")
            return UmlAccess::Public;
        if (spec == "protected")
            return UmlAccess::Protected;
        return UmlAccess::Private;
    };

    UmlClass* currentClass = nullptr;
    std::vector<std::string> pendingDeps;

    auto flushDeps = [&]()
    {
        if (currentClass)
        {
            for (const std::string& target : pendingDeps)
                addRelationshipOnce(context, currentClass->name(), target, RelationshipType::Dependency);
        }
        pendingDeps.clear();
    };

    // Проход 2: Разбираем поля, свойства, методы и связи C#
    for (const std::string& rawLine : lines)
    {
        const std::string trimmed = trim(rawLine);
        if (trimmed.empty() || trimmed.substr(0, 2) == "//" || trimmed.substr(0, 5) == "using")
            continue;

        std::smatch match;

        if (std::regex_search(rawLine, match, classRegex))
        {
            flushDeps();
            const std::string className = match[1].str();
            currentClass = diagram.findClassMutable(className);
            context.currentClass = currentClass;

            const std::string bases = match[2].str();
            for (const std::string& base : collectReferencedClasses(bases, context))
            {
                addRelationshipOnce(context, className, base, RelationshipType::Inheritance);
            }
            continue;
        }

        if (!currentClass)
            continue;

        // Метод C#
        if (std::regex_search(rawLine, match, methodRegex))
        {
            const std::string accessStr = match[1].str();
            const std::string retType = trim(match[2].str());
            const std::string methodName = match[3].str();
            const std::string params = trim(match[4].str());

            if (retType != "class" && retType != "interface" && retType != "namespace")
            {
                std::string formatted = methodName + "(" + params + ")";
                if (!retType.empty() && retType != "void")
                    formatted += ": " + retType;

                currentClass->addMethod(formatted, csAccess(accessStr));

                for (const std::string& target : collectReferencedClasses(retType + " " + params, context))
                    pendingDeps.push_back(target);

                continue;
            }
        }

        // Конструктор C#
        if (std::regex_search(rawLine, match, ctorRegex))
        {
            const std::string accessStr = match[1].str();
            const std::string ctorName = match[2].str();
            const std::string params = trim(match[3].str());

            if (ctorName == currentClass->name())
            {
                currentClass->addMethod(ctorName + "(" + params + ")", csAccess(accessStr));
                for (const std::string& target : collectReferencedClasses(params, context))
                    pendingDeps.push_back(target);
                continue;
            }
        }

        // Поле или свойство C#
        if (std::regex_search(rawLine, match, fieldOrPropRegex))
        {
            const std::string accessStr = match[1].str();
            const std::string fieldType = trim(match[2].str());
            const std::string fieldName = match[3].str();

            if (fieldType != "return" && fieldType != "class" && fieldType != "namespace")
            {
                currentClass->addAttribute(fieldName + ": " + fieldType, csAccess(accessStr));

                const bool isColl = isCollectionType(fieldType);
                const RelationshipType relType = isColl
                                                     ? RelationshipType::Aggregation
                                                     : RelationshipType::Association;
                const std::string tgtMult = isColl ? "0..*" : "1";

                for (const std::string& target : collectReferencedClasses(fieldType, context))
                {
                    addRelationshipOnce(context, currentClass->name(), target, relType, "1", tgtMult);
                }
            }
        }
    }

    flushDeps();
    return true;
}

} // namespace

// -------------------------------------------------------------------------
// БЛОК 5. Главная точка входа парсера (Автовыбор C++ / Python / C#)
// -------------------------------------------------------------------------

bool CppParser::parseFile(
    const std::string& filePath,
    UmlDiagram& diagram,
    std::string& errorMessage)
{
    errorMessage.clear();

    if (filePath.empty())
    {
        errorMessage = "Не указан путь к исходному файлу.";
        return false;
    }

    // Автоматически определяем язык программирования по расширению файла!
    if (endsWith(filePath, ".py"))
    {
        return parsePythonFile(filePath, diagram, errorMessage);
    }

    if (endsWith(filePath, ".cs"))
    {
        return parseCSharpFile(filePath, diagram, errorMessage);
    }

    // Иначе разбираем как файл C / C++ через libclang
    CXIndex index = clang_createIndex(0, 0);
    if (!index)
    {
        errorMessage = "Не удалось создать индекс Clang.";
        return false;
    }

    std::vector<std::string> argumentStorage = {
        "-x",
        "c++",
        "-std=c++17",
        "-stdlib=libc++"
    };

#ifdef UML_MACOS_SDK_PATH
    const std::string sdkPath = UML_MACOS_SDK_PATH;
    argumentStorage.push_back("-isysroot");
    argumentStorage.push_back(sdkPath);
    argumentStorage.push_back("-isystem");
    argumentStorage.push_back(sdkPath + "/usr/include/c++/v1");
    argumentStorage.push_back("-isystem");
    argumentStorage.push_back(sdkPath + "/usr/include");
#endif

#ifdef __APPLE__
    std::string resourceDir;

#ifdef UML_CLANG_RESOURCE_DIR
    resourceDir = UML_CLANG_RESOURCE_DIR;
#endif

    if (resourceDir.empty())
    {
        const char* resourceEnvironment = std::getenv("UML_CLANG_RESOURCE_DIR");
        if (resourceEnvironment && *resourceEnvironment)
            resourceDir = resourceEnvironment;
    }

    if (resourceDir.empty())
    {
        resourceDir =
            "/Applications/Xcode.app/Contents/Developer/"
            "Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/21";
    }

    argumentStorage.push_back("-resource-dir");
    argumentStorage.push_back(resourceDir);
    argumentStorage.push_back("-isystem");
    argumentStorage.push_back(resourceDir + "/include");
#endif

    std::vector<const char*> arguments;
    arguments.reserve(argumentStorage.size());
    for (const std::string& argument : argumentStorage)
        arguments.push_back(argument.c_str());

    CXTranslationUnit translationUnit = clang_parseTranslationUnit(
        index,
        filePath.c_str(),
        arguments.data(),
        static_cast<int>(arguments.size()),
        nullptr,
        0,
        CXTranslationUnit_None);

    if (!translationUnit)
    {
        clang_disposeIndex(index);
        errorMessage = "Clang не смог разобрать файл: " + filePath;
        return false;
    }

    const unsigned diagnosticCount = clang_getNumDiagnostics(translationUnit);
    for (unsigned i = 0; i < diagnosticCount; ++i)
    {
        CXDiagnostic diagnostic = clang_getDiagnostic(translationUnit, i);
        if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error)
        {
            const std::string message = toString(clang_formatDiagnostic(
                diagnostic, clang_defaultDiagnosticDisplayOptions()));
            if (!errorMessage.empty())
                errorMessage += "\n";
            errorMessage += message;
        }
        clang_disposeDiagnostic(diagnostic);
    }

    CXCursor translationUnitCursor = clang_getTranslationUnitCursor(translationUnit);

    ClassCollector classCollector;
    classCollector.diagram = &diagram;
    clang_visitChildren(translationUnitCursor, collectClasses, &classCollector);

    ClassContext context;
    context.diagram = &diagram;
    context.knownClasses = classCollector.knownClasses;

    ClassMemberCollector memberCollector;
    memberCollector.context = &context;
    clang_visitChildren(translationUnitCursor, visitClassDefinition, &memberCollector);

    clang_disposeTranslationUnit(translationUnit);
    clang_disposeIndex(index);

    return errorMessage.empty();
}