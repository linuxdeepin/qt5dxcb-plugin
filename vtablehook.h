#ifndef VTABLEHOOK_H
#define VTABLEHOOK_H

#include <QObject>
#include <QSet>
#include <QDebug>

class VtableHook
{
public:
    static bool copyVtable(qintptr **obj);
    static bool clearGhostVtable(void *obj);

    template <typename List1, typename List2> struct CheckCompatibleArguments { enum { value = false }; };
    template <typename List> struct CheckCompatibleArguments<List, List> { enum { value = true }; };
    template<typename Fun1, typename Fun2>
    static bool overrideVfptrFun(const typename QtPrivate::FunctionPointer<Fun1>::Object *t1, Fun1 fun1,
                      const typename QtPrivate::FunctionPointer<Fun2>::Object *t2, Fun2 fun2)
    {
        if (!objToOriginalVfptr.contains((qintptr**)t1) && !copyVtable((qintptr**)t1))
            return false;

        //! ({code}) in the form of a code is to eliminate - Wstrict - aliasing build warnings
        qintptr fun1_offset = ({qintptr *f = (qintptr *)&fun1; *f - 1;});
        qintptr fun2_offset = ({qintptr *f = (qintptr *)&fun2; *f - 1;});

        if (fun1_offset < 0 || fun1_offset > UINT_LEAST16_MAX || fun2_offset < 0)
            return false;

        typedef QtPrivate::FunctionPointer<Fun1> FunInfo1;
        typedef QtPrivate::FunctionPointer<Fun2> FunInfo2;

        //compilation error if the arguments does not match.
        Q_STATIC_ASSERT_X((CheckCompatibleArguments<typename FunInfo1::Arguments, typename FunInfo2::Arguments>::value),
                          "Function1 and Function2 arguments are not compatible.");
        Q_STATIC_ASSERT_X((CheckCompatibleArguments<QtPrivate::List<typename FunInfo1::ReturnType>, QtPrivate::List<typename FunInfo2::ReturnType>>::value),
                          "Function1 and Function2 return type are not compatible..");

        qintptr *vfptr_t1 = *(qintptr**)t1;
        qintptr *vfptr_t2 = *(qintptr**)t2;

        if (fun2_offset > UINT_LEAST16_MAX)
            *(qintptr*)((char*)vfptr_t1 + fun1_offset) = fun2_offset + 1;
        else
            *(qintptr*)((char*)vfptr_t1 + fun1_offset) = *(qintptr*)((char*)vfptr_t2 + fun2_offset);

        return true;
    }

    template<typename Fun1>
    static bool resetVfptrFun(const typename QtPrivate::FunctionPointer<Fun1>::Object *t1, Fun1 fun1)
    {
        qintptr *vfptr_t2 = objToOriginalVfptr.value((qintptr**)t1, 0);

        if (!vfptr_t2)
            return false;

        //! ({code}) in the form of a code is to eliminate - Wstrict - aliasing build warnings
        qintptr fun1_offset = ({qintptr *f = (qintptr *)&fun1; *f - 1;});

        if (fun1_offset < 0 || fun1_offset > UINT_LEAST16_MAX)
            return false;

        qintptr *vfptr_t1 = *(qintptr**)t1;

        *(qintptr*)((char*)vfptr_t1 + fun1_offset) = *(qintptr*)((char*)vfptr_t2 + fun1_offset);

        return true;
    }

private:
    static QHash<qintptr**, qintptr*> objToOriginalVfptr;
    static QHash<void*, qintptr*> objToGhostVfptr;
};

#endif // VTABLEHOOK_H
