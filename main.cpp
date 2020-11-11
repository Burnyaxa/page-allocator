#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <iostream>

#define PAGE_SIZE 4096 // 4 kb
#define MEMORY_SIZE 16384 //16 kb
#define PAGE_AMOUNT ceil(MEMORY_SIZE / PAGE_SIZE)
#define MIN_CLASS_SIZE 16 // 2^x where x >= 4
#define BLOCK_HEADER_SIZE 1 //bool busy or not

static void* startPointer;

enum class pageStatus {
	Free, 
	Divided, //if the block size <= PAGE_SIZE / 2
	MultipageBlock // if the block size > PAGE_SIZE / 2
};

#pragma pack(push, 1)
struct memoryPageHeader {
	pageStatus status;
	size_t classSize; //size of each block inside a page or pages if multipage block 
	uint8_t* availableBlock; //pointer to a free block inside a page, nullptr if no available block, pointer to the next page's block if multipage block 
};

static char memory[MEMORY_SIZE];
static std::vector<void*> freePages; //free pages array
static std::map<void*, memoryPageHeader> headers; // headers map : key - page pointer, value - header
static std::map <int, std::vector<void*>> classifiedPages; // map of allowed classes size : key - size, value - page, only available pages stored here, after its full, it should be deleted

void initializePages() {
	startPointer = memory;
	for (int i = 0; i < PAGE_AMOUNT; i++)
	{
		void* pagePointer = (uint8_t*)startPointer + PAGE_SIZE * i;
		freePages.push_back(pagePointer);
		memoryPageHeader header = { pageStatus::Free, 0, nullptr };
		headers.insert({pagePointer, header});
	}

	for (int classSize = MIN_CLASS_SIZE; classSize <= PAGE_SIZE / 2; classSize *= 2) 
	{
		classifiedPages.insert({ classSize, {} });
	}
}

//align number to the minimum power of two e.g. 55 -> 64 
size_t powerOfTwoAligment(size_t number) {  
	size_t minValue = 1;

	while (minValue <= number)
	{
		minValue *= 2;
	}

	return minValue;
}

void setPageHeader(void* pagePointer, pageStatus status, void* blockPointer, size_t classSize) {
	headers[pagePointer].status = status;
	headers[pagePointer].availableBlock = (uint8_t*)blockPointer;
	headers[pagePointer].classSize = classSize;
}

void setBlockHeader(void* blockPointer, bool status) {
	uint8_t* pointer = (uint8_t*)blockPointer;
	*pointer = status;
}

//find a free page block and return a pointer or nullptr if not found
void* anyFreeBlock(void* pagePointer, size_t classSize) {
	for (uint8_t* cursor = (uint8_t*)pagePointer; cursor != (uint8_t*)pagePointer + PAGE_SIZE; cursor += classSize)
	{
		if ((bool)*cursor == true)
		{
			return cursor + BLOCK_HEADER_SIZE;
		}
	}
	return nullptr;
}

// check if every block free, used in mem_free to check if it is possible to add a page to the freePages array
bool isEveryBlockFree(void* pagePointer, size_t classSize) {
	for (uint8_t* cursor = (uint8_t*)pagePointer; cursor != (uint8_t*)pagePointer + PAGE_SIZE; cursor += classSize)
	{
		if ((bool)*cursor == false) 
		{
			return false;
		}
	}
	return true;
}

// set class size to a page and decide how many blocks can store the page
bool setBlocksSize(size_t classSize) {
	if (freePages.empty())
	{
		return false;
	}

	uint8_t* freePage = (uint8_t*)freePages[0];
	
	for (uint8_t* cursor = freePage; cursor != freePage + PAGE_SIZE; cursor += classSize)
	{
		*cursor = true;
	}

	freePages.erase(freePages.begin());
	setPageHeader(freePage, pageStatus::Divided, freePage + BLOCK_HEADER_SIZE, classSize);
	classifiedPages[classSize].push_back(freePage);

	return true;
}

void* mem_alloc(size_t size) {
	void* pointer = nullptr;

	if (size == 0)
	{
		return nullptr;
	}

	if (size <= PAGE_SIZE / 2) 
	{
		size_t classSize = powerOfTwoAligment(size + BLOCK_HEADER_SIZE);

		if (classifiedPages[classSize].empty() && !setBlocksSize(classSize)) 
		{
			return nullptr;
		}

		uint8_t* page = (uint8_t*)classifiedPages[classSize].at(0);
		pointer = headers[page].availableBlock;
		setBlockHeader(headers[page].availableBlock - BLOCK_HEADER_SIZE, false);

		uint8_t* freeBlock = (uint8_t*)anyFreeBlock(page, classSize);

		if (freeBlock != nullptr)
		{
			headers[page].availableBlock = freeBlock;
		}
		else
		{
			classifiedPages[classSize].erase(std::find(classifiedPages[classSize].begin(),
				classifiedPages[classSize].end(), page));
		}
	}
	else
	{
		int pagesNeeded = ceil((double)size / PAGE_SIZE);

		if (freePages.size() < pagesNeeded)
		{
			return nullptr;
		}

		pointer = freePages[0];

		for (int i = 1; i <= pagesNeeded; i++) 
		{
			uint8_t* page = (uint8_t*)freePages[0];
			uint8_t* nextPage = pagesNeeded == i ? nullptr : (uint8_t*)freePages[1];
			if (i != pagesNeeded)
			{
				setPageHeader(page, pageStatus::MultipageBlock, nextPage + BLOCK_HEADER_SIZE, pagesNeeded * PAGE_SIZE);
			}
			else
			{
				setPageHeader(page, pageStatus::MultipageBlock, nullptr, pagesNeeded * PAGE_SIZE);
			}
			freePages.erase(freePages.begin());
		}
	}
	return pointer;
}

void mem_free(void* addr) {
	if (addr == nullptr)
	{
		return;
	}

	if ((uint8_t*)addr < (uint8_t*)startPointer || (uint8_t*)addr >(uint8_t*)startPointer + MEMORY_SIZE) {
		return;
	}

	size_t pageIndex = ((uint8_t*)addr - (uint8_t*)startPointer) / PAGE_SIZE;
	uint8_t* page = (uint8_t*)startPointer + pageIndex * PAGE_SIZE;
	double amountOfPages = ceil(headers[page].classSize / PAGE_SIZE);

	if (headers[page].status == pageStatus::Divided)
	{
		uint8_t* block = (uint8_t*)addr - BLOCK_HEADER_SIZE;
		*block = true;

		if (isEveryBlockFree(page, headers[page].classSize))
		{
			classifiedPages[headers[page].classSize].erase(std::find(classifiedPages[headers[page].classSize].begin(),
				classifiedPages[headers[page].classSize].end(), page));
			setPageHeader(page, pageStatus::Free, nullptr, 0);
			freePages.push_back(page);
		}
		if (std::find(classifiedPages[headers[page].classSize].begin(),
			classifiedPages[headers[page].classSize].end(), page) == classifiedPages[headers[page].classSize].end())
		{
			classifiedPages[headers[page].classSize].push_back(page);
		}
	}

	if (headers[page].status == pageStatus::MultipageBlock)
	{
		for (int i = 0; i < amountOfPages; i++) 
		{
			uint8_t* nextPage = headers[page].availableBlock - BLOCK_HEADER_SIZE;
			setPageHeader(page, pageStatus::Free, nullptr, 0);
			freePages.push_back(page);
			page = nextPage;
		}
	}
}
void* mem_realloc(void* addr, size_t size) {
	if (addr == nullptr)
	{
		return mem_alloc(size);
	}

	if ((uint8_t*)addr < (uint8_t*)startPointer || (uint8_t*)addr >(uint8_t*)startPointer + MEMORY_SIZE) {
		return nullptr;
	}

	void* pointer = addr;
	size_t pageIndex = ((uint8_t*)addr - (uint8_t*)startPointer) / PAGE_SIZE;
	uint8_t* page = (uint8_t*)startPointer + pageIndex * PAGE_SIZE;

	if (headers[page].status == pageStatus::Divided)
	{
		size_t classSize = powerOfTwoAligment(size);

		if (headers[page].classSize == classSize)
		{
			return addr;
		}

		pointer = mem_alloc(size);

		if (pointer != nullptr)
		{
			mem_free(addr);
			return pointer;
		}
		return addr;
	}
	if (headers[page].status == pageStatus::MultipageBlock)
	{
		size_t sizeOld = headers[page].classSize;
		double amountOfPagesOld = ceil(headers[page].classSize / PAGE_SIZE);
		double pagesNeeded = powerOfTwoAligment(size) / PAGE_SIZE;

		if (amountOfPagesOld == pagesNeeded) 
		{
			return addr;
		}

		if (size <= PAGE_SIZE / 2)
		{
			mem_free(addr);
			pointer = mem_alloc(size);
			return pointer;

		}

		if (amountOfPagesOld < pagesNeeded)
		{
			if (pagesNeeded - amountOfPagesOld > freePages.size())
			{
				return nullptr;
			}

			uint8_t* nextPage;

			for (int i = 1; i <= pagesNeeded; i++)
			{
				nextPage = headers[page].availableBlock - BLOCK_HEADER_SIZE;
				if(i >= amountOfPagesOld)
				{
					page = (uint8_t*)freePages[0];
					nextPage = pagesNeeded == i ? nullptr : (uint8_t*)freePages[1];
					if (i != pagesNeeded) {
						setPageHeader(page, pageStatus::MultipageBlock, nextPage + BLOCK_HEADER_SIZE, pagesNeeded * PAGE_SIZE);
					}
					else
					{
						setPageHeader(page, pageStatus::MultipageBlock, nullptr, pagesNeeded * PAGE_SIZE);
					}
					freePages.erase(freePages.begin());
				}
				page = nextPage;
			}
			return addr;
		}
		if (amountOfPagesOld > pagesNeeded)
		{
			uint8_t* nextPage;
			for (int i = 1; i <= amountOfPagesOld; i++)
			{
				nextPage = headers[page].availableBlock - BLOCK_HEADER_SIZE;
				if (i > pagesNeeded)
				{
					setPageHeader(page, pageStatus::Free, nullptr, 0);

					freePages.push_back(page);
				}
				else
				{
					if (i != pagesNeeded)
					{
						setPageHeader(page, pageStatus::MultipageBlock, nextPage + BLOCK_HEADER_SIZE, PAGE_SIZE * pagesNeeded);
					}
					else
					{
						setPageHeader(page, pageStatus::MultipageBlock, nullptr, PAGE_SIZE * pagesNeeded);
					}
				}
				page = nextPage;
			}
			return addr;
		}
		
	}
}


void mem_dump() {
	std::cout << "-----------------------------------" << std::endl;
	uint8_t* page = (uint8_t*)startPointer;
	for (int i = 0; i < PAGE_AMOUNT; i++)
	{
		memoryPageHeader header = headers[page];

		std::string state;

		switch (header.status)
		{
		case pageStatus::Free:
			state = "Free";
			break;
		case pageStatus::Divided:
			state = "Divided";
			break;
		case pageStatus::MultipageBlock:
			state = "MultiPageBlock";
			break;
		}

		std::cout << "PAGE " << i << std::endl;
		std::cout << "Address: " << (uint16_t*)page << std::endl;
		std::cout << "Status: " << state << std::endl;
		std::cout << "Page size: " << PAGE_SIZE << std::endl;

		if (header.status == pageStatus::Divided)
		{
			std::cout << "Class size: " << header.classSize << std::endl;

			for (int j = 0; j < PAGE_SIZE / header.classSize; j++)
			{
				uint8_t* blockAddress = page + header.classSize * j + BLOCK_HEADER_SIZE;
				uint8_t* isOccupied = blockAddress - BLOCK_HEADER_SIZE;
				std::cout << "BLOCK " << j << std::endl;
				std::cout << "Address " << (uint16_t*)blockAddress << std::endl;
				std::cout << "Free " << (bool)*isOccupied << std::endl;
			}
		}
		if (header.status == pageStatus::MultipageBlock)
		{
			std::cout << "Block size: " << header.classSize << std::endl;
			std::cout << "Next block: " << (uint16_t*)header.availableBlock << std::endl;
		}
		page += PAGE_SIZE;
		std::cout << "-----------------------------------" << std::endl;
	}

}
int main() {
	initializePages();
	void* x1 = mem_alloc(9000);
	void* x2 = mem_alloc(400);
	void* x3 = mem_alloc(400);
	mem_dump();
	mem_free(x2);
	mem_dump();
	void* x5 = mem_realloc(x1, 5000);
	void* x6 = mem_realloc(x3, 1000);
	mem_dump();
	return 0;
}