// Kotlin code run by test_vm on henge-vm (compiled with kotlinc, shrunk with R8, dexed).
package kt

class Vec(val x: Int, val y: Int) {
    operator fun plus(o: Vec) = Vec(x + o.x, y + o.y)
    override fun toString() = "($x, $y)"
}

data class Person(val name: String, val age: Int)

interface Animal { fun sound(): String; fun describe() = "I say ${sound()}" }
class Dog : Animal { override fun sound() = "woof" }
class Cat : Animal { override fun sound() = "meow"; override fun describe() = "cat: ${sound()}" }

sealed class Expr
class Num(val v: Int) : Expr()
class Add(val l: Expr, val r: Expr) : Expr()
class Mul(val l: Expr, val r: Expr) : Expr()

fun eval(e: Expr): Int = when (e) {
    is Num -> e.v
    is Add -> eval(e.l) + eval(e.r)
    is Mul -> eval(e.l) * eval(e.r)
}

class Counter {
    companion object { var created = 0 }
    var count = 0
    init { created++ }
    fun inc() = ++count
}

var failures = 0

fun check(what: String, ok: Boolean) {
    println((if (ok) "PASS  " else "FAIL  ") + what)
    if (!ok) failures++
}

fun nonNull(s: String, n: Int): Int = s.length + n

fun maybe(i: Int): String? = if (i > 0) "pos" else null

fun sumTo(n: Int): Int {
    var s = 0
    for (i in 1..n) s += i
    return s
}

fun fact(n: Long): Long = if (n <= 1) 1 else n * fact(n - 1)

fun run(): Int {
    failures = 0
    check("kotlin hello", "hello".uppercase() == "HELLO")
    check("operator plus / template", (Vec(1, 2) + Vec(3, 4)).toString() == "(4, 6)")
    val p = Person("Ann", 30)
    check("data class", p.toString() == "Person(name=Ann, age=30)" && p == Person("Ann", 30) && p.copy(age = 31).age == 31)
    check("data hashCode", p.hashCode() == Person("Ann", 30).hashCode())
    check("interface default", Dog().describe() == "I say woof" && Cat().describe() == "cat: meow")
    check("sealed / when", eval(Add(Num(2), Mul(Num(3), Num(4)))) == 14)
    val c = Counter()
    c.inc(); c.inc()
    check("companion / init", c.count == 2 && Counter.created == 1)
    check("not-null param", nonNull("abc", 4) == 7)
    var threw = false
    try { nonNull(maybe(0)!!, 1) } catch (e: NullPointerException) { threw = true }
    check("!! throws NPE", threw)
    check("elvis / safe call", (maybe(0) ?: "none") == "none" && maybe(5)?.length == 3)
    check("range loops", sumTo(100) == 5050 && (0 until 5).sum() == 10)
    check("long math", fact(15) == 1307674368000L)
    var acc = 0
    repeat(5) { acc += it }
    check("repeat / inline lambda", acc == 10)
    val sq = { x: Int -> x * x }
    check("lambda value", sq(7) == 49)
    check("split", "a,b,c".split(",").size == 3)
    println("reversed=[" + "kotlin".reversed() + "] sb=[" + java.lang.StringBuilder("kotlin").reverse() + "]")
    check("reversed", "kotlin".reversed().toString() == "niltok")
    check("repeat", "x".repeat(3) == "xxx")
    check("when expression", (when (3) { 1 -> "a"; 2, 3 -> "b"; else -> "c" }) == "b")
    check("char / string ops", 'a'.code == 97 && "abc".first() == 'a' && "abc".last() == 'c')
    check("pair / destructure", run { val (a, b) = Pair(1, "z"); a == 1 && b == "z" })
    return failures
}
