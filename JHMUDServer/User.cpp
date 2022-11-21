#include <iostream>
#include <list>
#include <stdlib.h>
#include <string>
#include <windows.h>
#include "User.h"
#include "Slime.h"	
using namespace std;


	User::User(string _nickname)
	{
		nickname.replace(0,_nickname.size(),_nickname);
		x = rand() % 30;
		y = rand() % 30;
		hp = 30;
		str = 30;
		numOfHpPotion = 1;
		numOfStrPotion = 1;
	}
	User::User(string _nickname, int _x, int _y, int _hp, int _str, int _numOfHpPotion, int _numOfStrPotion)
	{
		nickname.replace(0, _nickname.size(), _nickname);
		x = _x;
		y = _y;
		hp = _hp;
		str = _str;
		numOfHpPotion = _numOfHpPotion;
		numOfStrPotion = _numOfStrPotion;
	}
	User::~User() {  }



	bool User::Move(int _x, int _y)
	{
		if (x + _x > 29 || x + _x < 0 || y + _y > 29 || y + _y < 0)
		{
			return false;
		}
		else
		{
			x += _x;
			y += _y;
			return true;
		}
	}

	void User::Attack(list<Slime*> slimes)
	{
		list<Slime*>::iterator iter;
		for (iter = slimes.begin(); iter != slimes.end(); iter++)
		{
			int ux = (*iter)->getX();
			int uy = (*iter)->getY();
			if ((ux >= x - 1) && (ux <= x + 1) && (uy >= y - 1) && (uy <= y + 1))
			{
				int item = (*iter)->SlimeDamaged(str, nickname);
				if (item == 1) { numOfHpPotion++; cout << "\n\n Ã¼·Â Æ÷¼Ç È¹µæ!\n"; }
				else if (item == 2) {numOfStrPotion++; cout << "\n\n Èû °­È­ Æ÷¼Ç È¹µæ!\n";}
				
			}
		}
	}

	bool User::UserDamaged(int _damage)  //µ¥¹ÌÁö ÀÔÀ»½Ã true »ç¸Á½Ã false
	{
		hp -= _damage;
		if (hp <= 0)
		{
			return false;
		}
		return true;
	}



int User::getX() { return x; }
int User::getY() { return y; }
int User::getHp() { return hp; }
int User::getStr() { return str; }
int User::getNumOfHpPotion() { return numOfHpPotion; }
int User::getNumOfStrPotion() { return numOfStrPotion; }
string User::getNickname() { return nickname; }
