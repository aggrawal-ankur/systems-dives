# There are no data types, only bits, widths and interpretations.

**Note1: Everything in this writeup is strictly in accord with the ISO C 9889:2024 (C23) draft, which is accessible at [open-std.org](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf)** .

**Note2: Some content in this writeup draws from Jens Gustedt. Modern C. Manning, 2025, 9781633437777. ⟨hal-02383654v2⟩, available at [inria.hal.science](https://inria.hal.science/hal-02383654v2/file/modernC.pdf)**

**Note3: Everything is a number in C, so there is no heading dedicated to characters or strings. This looks counterintuitive, but proved correct later in the writeup.**

**Note4: This writeup doesn't include floats because they belong to a separate world and deserves a separate writeup.**

---

Every programming language has data types.

A data type is a container with **width** as a property. The wider the container is, the larger the range of values it can hold.

As beginners, we are told that "an int is sized 4 bytes". Some tutorials might say that "the size of an int is implementation-defined, but 4 bytes on most machines".
  - None of the statements are completely right or wrong.
  - These statements are correct **with hidden clauses**, which we will uncover soon.

Although "size" is not a wrong name, the standard uses "width". Therefore, we will use "width" in this writeup whenever we have to talk about the "size" of a data type.

---

For the machine, everything is an integer with a bit-pattern. It can't distinguish between a character, a digit, or an emoji. It is the interpretation of this bit-pattern that makes something a number, a character or anything else.

Format specifiers decide the interpretation of the value stored at a portion of memory.

Here is a complete list of data types in C.

## Standard Integer Types

They include standard signed integer types and the corresponding standard unsigned integer types, with an exception of `bool` which is only an unsigned integer type.

### Standard Signed Integer Types

There are five standard signed integer types:
  1. signed char, 
  2. short int, 
  3. int, 
  4. long int, and
  5. long long int

| Data Type | Other Names | Format Specifier | Minimum guaranteed width |
| :-------- | :---------- | :--------------- | :----------------------- |
| signed char |  | %c | >= 8 bits |
| short | short int, signed short, signed short int | %hi | >= 16 bits |
| int | signed, signed int | %i or %d | >= 16 bits |
| long | long int, signed long, signed long int | %ld or %li | >= 32 bits |
| long long | long long int, signed long long, signed long long int | %lld or %lli | >= 64 bits |

For each of the signed integer types, there is a corresponding unsigned integer type.

### Standard Unsigned Integer Types

| Data Type | Other Names | Format Specifier | Minimum guaranteed width |
| :-------- | :---------- | :--------------- | :----------------------- |
| bool |  | %d | exactly 1 bit | true and false | 2 |
| unsigned char |  | %c | >= 8 bits | [0, (2^8)-1] | 256 |
| unsigned short | unsigned short int | %hu | >= 16 bits | [0, (2^16)-1] | 65,536 |
| unsigned int |  | %u | >= 16 bits | [0, (2^32)-1] | 4,294,967,296 |
| unsigned long | unsigned long int | %lu | >= 32 bits | 4b: [-(2^31), (2^31)-1] | 4,294,967,296 |
| unsigned long long | unsigned long long int | %llu | >= 64 bits | [-(2^64), (2^64)-1] | 18,446,744,073,709,551,616 |

## Bit-Precise Integer Type

A _BitInt(N) is a bit-precise integer type, where N is an integer that specifies the number of bits that are used to represent the type.

`_BitInt(N)` alone is signed and `unsigned _BitInt(N)` is unsigned.

## Extended Integer Types

These are the types that an implementation (the compiler) supports based on hardware availability. The standard doesn't define them, but acknowledges their existence.

For example, gcc/clang has __int128 on x64, which is a 128-bit integer type.

They can be both signed and unsigned.

---

The standard signed integer types, bit-precise signed integer types, and extended signed integer types are collectively called **signed integer types**.

The standard unsigned integer types, bit-precise unsigned integer types, and extended unsigned integer types are collectively called **unsigned integer types**.

The type char, the signed and unsigned integer types, and the floating types are collectively called **the basic types**.

## The Two Questions

**Ques1**: Why the tables above lack the standalone `char` data type?

**Ques2**: Why we are talking in terms of "minimum width" when sizeof(type) returns an equatable width?

### The absence of `char`

The standalone `char` data type is absent in the tables because the C standard defines it in a way which makes it hard to fit in any of the tables above.

This is paragraph #3, under section 6.2.5, on page 38:
```
An object declared as type char is large enough to store any member of the basic execution character set. If a member of the basic execution character set is stored in a char object, its value is guaranteed to be nonnegative. If any other character is stored in a char object, the resulting value is implementation-defined but shall be within the range of values that can be represented in that type.
```

Section 5.2.1, starting from page 19, defines character sets in detail. Here are those paragraphs simplified.

-> **Paragraph #1**

We have two characters sets:
  1. *Source character set*: the character set in which source files are written.
  2. *Execution character set*: the character set that ends up in the execution environment.

Each set is further divided into a basic character set and extended character set.

-> **Paragraph #3**

Both the basic source and basic execution character sets have the following characters:
  - The 26 uppercase letters of the Latin alphabet.
  - The 26 lowercase letters of the Latin alphabet.
  - The 10 digits of the decimal system, i.e [0, 9].
  - The 32 graphic/special characters start_( !  "  #  %  &  '  (  )  *  +  ,  -  .  /  :  ;  <  =  >  ?  [  \  ]  ^  _  {  |  }  ~  @  $  ` )_end.
  - The white space characters (space, horizontal tab (\t), vertical tab (\v), and form feed(\f)).
  - The control characters (non-printable characters used for formatting and program control). They include the null character (\0), alert/bell (\a), backspace (\b), carriage return (\r) and the newline character (\n).

The total number of characters are (26+26+10+32+4+5) 99.

---

The basic execution character set is defined now. Let's read the paragraph again.

"If a member of the basic execution character set is stored in a char object, its value is guaranteed to be nonnegative."
  - This is in accord with the 7-bit ASCII character encoding standard, where each of the 128 characters have a unique unsigned integer associated to them.

"If any other character is stored in a char object, the resulting value is implementation-defined but shall be within the range of values that can be represented in that type."
  - This is in accord with the 8-bit extension of ASCII, which allows for 256 combinations, which is what the standard is unsure of, so it acknowledges but doesn't confirm the signedness.
  - It simply says that anything beyond the basis execution set is not guaranteed to be nonnegative (or unsigned), but it is guaranteed to be in the range of possible values.
  - This means, we can have two combinations in 2's complement: [0, 255] for unsigned and [-128, +127] in signed.
  - Notice that both the combinations guarantees to have the basic execution set characters as nonnegative, which conforms to the standard.

For this reason, the standalone `char` data type doesn't fit any table.

Point #20 on page 40 further strengthens this claim:
```
The three types char, signed char, and unsigned char are collectively called the character types. The implementation shall define char to have the same range, representation, and behavior as either signed char or unsigned char.
```

### The standard's guarantee v/s The reality

The ISO C standard is a formal specification which specifies the minimum contract that a C implementation should obey to be considered a "standard implementation". 

The spec is architecture-agnostic and doesn't account for the targeted ABI or the hardware availability because these constraints are chosen by an implementation.

The implementation of the spec depends on a variety of constraints which we often refer to as "implementation-defined". Beneath that phrase are the actual constraints that influence the final shape of "a standard's guarantee's actual implementation".

---

The result of `sizeof(type)` is the final answer to "what is the actual size of this data type on this machine, given all the constraints".

Therefore, "implementation-defined" is not "unexplainable stuff".

## Type definition based integers

Under section 6.2.5, on page 40, pt.19 acknowledges the presence of type definition based integers.
```
An implementation can define new keywords that provide alternative ways to designate a basic (or any other) type; this does not violate the requirement that all basic types be different. Implementation-defined keywords have the form of an identifier reserved for any use.
```

The C standard defines a header file called `stdint.h` (since C99) and every implementation (gcc/clang/msvc) provide it. It provides a huge variety of size specific integer types, such as int_32t, uint_32t etc.

I'd recommend visiting this link on GitHub: [torvalds/linux/tools/include/nolibc/stdint.h](https://github.com/torvalds/linux/blob/master/tools/include/nolibc/stdint.h). It's a no BS file which defines each type definition without unnecessary stuff.

If you want to find the one(s) available on your system, you can use two methods:
  - **Method 1:**
    ```bash
    find / -name 2>/dev/null stdint.h
    ```
  - **Method 2:** Create a .c file and include the stdint.h file. Now hover over the file name, press alt and click. If you have clang installed, you might get redirected to it's implementation, which is one complete file. In case of gcc, you have to crawl and find `stdint-gcc.h` instead.





# Rank and typecasting

# Prove everything is a number with format specifiers.

<limits.h>

hierarchy: bool < char < short < int < long < long long

actual widths are implementation defined. (prove it by finding implementations where they are different, like char being 2)

**Note2: The range of possible values is calculated using these formulas, which are based on 2's complement,**
```bash
# Unsigned Range
[0, (2^n)-1]

# Signed Range
[-(2^(n-1)), (2^(n-1))-1] 

n is the width of the type in bits.
```
