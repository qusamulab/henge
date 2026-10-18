package java.lang;

import java.lang.String;

public interface CharSequence {
    int length();
    char charAt(int index);
    CharSequence subSequence(int start, int end);
    String toString();
}
