
#ifndef PARSEDATA_H_INCLUDED
#define PARSEDATA_H_INCLUDED

#include <string>
#include <vector>

class ParseData {
    std::vector<std::string> field;
    char start_ch;
    char end_ch;
    char separator;

public:
    ParseData()
    {
        SetStyle('[',']','|');
    }

    ParseData(char startChar,char endChar,char separatorChar)
    {
        SetStyle(startChar,endChar,separatorChar);
    }

    void SetStyle(char startChar,char endChar,char separatorChar)
    {
        start_ch=startChar;
        end_ch=endChar;
        separator=separatorChar;
    }

    static std::string SetStrLen(std::string str,int n)
    {
        std::string ostr(n,'0');
        ostr+=str;
        return ostr.substr(ostr.length()-n,n);
    }

    static std::string i2nc(int i,int n)
    {
        std::string ostr(n,'0');
        ostr+=std::to_string(i);
        return ostr.substr(ostr.length()-n,n);
    }

    int Parse(std::string str)
    {
        field.clear();

        int startpos=str.find(start_ch);
        startpos++;

        int endpos=str.find(end_ch,startpos);
        if(endpos<0) endpos=str.length();

        int len=endpos-startpos;
        if(len<=0) return field.size();

        str=str.substr(startpos,len);

        while(true){
            startpos=str.find(separator);
            if(startpos>=0){
                field.push_back(str.substr(0,startpos));
                startpos++;
                if(startpos>=str.length()){
                    break;
                }
                str=str.substr(startpos);
            }
            else{
                if(!str.empty()){
                    field.push_back(str);
                }
                break;
            }
        }
        return field.size();
    }

    std::string Field(int number)
    {
        if(number < field.size())
            return field.at(number);
        return "";
    }
};

#endif