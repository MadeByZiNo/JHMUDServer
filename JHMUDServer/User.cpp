#include <iostream>
#include <list>
#include <stdlib.h>
#include <string>
#include <windows.h>
#include "User.h"
#include "Slime.h"	
using namespace std;


	User::User(char _nickname[])
	{
		strcpy(nickname,_nickname);
		x = rand() % 30;
		y = rand() % 30;
		hp = 30;
		str = 30;
		numOfHpPotion = 1;
		numOfStrPotion = 1;
	}
	User::User(char _nickname[], int _x, int _y, int _hp, int _str, int _numOfHpPotion, int _numOfStrPotion)
	{
		strcpy(nickname, _nickname);
		x = _x;
		y = _y;
		hp = _hp;
		str = _str;
		numOfHpPotion = _numOfHpPotion;
		numOfStrPotion = _numOfStrPotion;
	}
	User::~User(){}



	void User::Move(int _x, int _y)
	{
		if (_x > 3 || _x < -3 || _y > 3  ||_y < -3)
		{
			printf("\n\n-3 ~ 3 사이의 값만 이동할 수 있습니다");
			Sleep(1500);
		}
		else if (x + _x > 29 || x + _x < 0 || y + _y > 29 || y + _y < 0)
		{
			printf("\n\n던전 범위를 넘어갈 수 없습니다.");
			Sleep(1500);
		}
		else
		{
			x += _x;
			y += _y;
			std::cout << "\n\n 이동 후 좌표 : (" << x << "," << y << ")" << std::endl;
			Sleep(1500);
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
				if (item == 1) { numOfHpPotion++; cout << "\n\n 체력 포션 획득!\n"; }
				else if (item == 2) {numOfStrPotion++; cout << "\n\n 힘 강화 포션 획득!\n";}
				
			}
		}
	}

	void User::UserDamaged(int _damage)
	{
		hp -= _damage;
		if (hp < 0)
		{
			std::cout<<"\n\n캐릭터 사망 게임 종료"<<endl;
			delete this;
			exit(1);
		}
	}



void User::PrintUserLocation()
{
	std::cout << "\n\n닉네임 : " << nickname << "의 좌표 : (" << x << "," << y << ")" << std::endl;
}
const char* User::PrintUserInformation()
{
	char c[10000];
	sprintf(c,"닉네임 : %s  HP : %d  STR : %d  x : %d  y : %d  HpPotion : %d  StrPotion : %d\n\0",nickname,hp,str,x,y,numOfHpPotion,numOfStrPotion);
	return c;
}
int User::getX() { return x; }
int User::getY() { return y; }
int User::getHp() { return hp; }
int User::getStr() { return str; }
int User::getNumOfHpPotion() { return numOfHpPotion; }
int User::getNumOfStrPotion() { return numOfStrPotion; }
const char* User::getNickname() { return nickname; }
