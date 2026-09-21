// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "AI/CSBotBehavior.h"

#include "AI/CSBTNodes.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "GameFramework/Actor.h"

namespace CSBotBehavior
{
	const FName KeyTarget(TEXT("TargetActor"));
	const FName KeyLastKnownLocation(TEXT("LastKnownLocation"));
	const FName KeyLootTarget(TEXT("LootTarget"));
	const FName KeyPatrolLocation(TEXT("PatrolLocation"));

	namespace
	{
		void AddObjectKey(UBlackboardData* BB, FName Name)
		{
			FBlackboardEntry Entry;
			Entry.EntryName = Name;
			UBlackboardKeyType_Object* Type = NewObject<UBlackboardKeyType_Object>(BB);
			Type->BaseClass = AActor::StaticClass();
			Entry.KeyType = Type;
			BB->Keys.Add(Entry);
		}

		void AddVectorKey(UBlackboardData* BB, FName Name)
		{
			FBlackboardEntry Entry;
			Entry.EntryName = Name;
			Entry.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
			BB->Keys.Add(Entry);
		}

		template <typename T>
		T* Node(UBehaviorTree* Tree, const TCHAR* Name)
		{
			T* N = NewObject<T>(Tree);
			N->NodeName = Name;
			return N;
		}

		void AddTask(UBTCompositeNode* Parent, UBTTaskNode* Task)
		{
			FBTCompositeChild& Child = Parent->Children.AddDefaulted_GetRef();
			Child.ChildTask = Task;
		}

		void AddBranch(UBTCompositeNode* Parent, UBTCompositeNode* Branch, UBTDecorator* Condition)
		{
			FBTCompositeChild& Child = Parent->Children.AddDefaulted_GetRef();
			Child.ChildComposite = Branch;
			if (Condition)
			{
				Child.Decorators.Add(Condition);
			}
		}

		UCSBTDecorator_KeySet* KeyIsSet(UBehaviorTree* Tree, FName Key)
		{
			UCSBTDecorator_KeySet* Decorator = NewObject<UCSBTDecorator_KeySet>(Tree);
			Decorator->Configure(Key, /*bMustBeSet*/ true);
			return Decorator;
		}

		UCSBTTask_MoveToKey* MoveTo(UBehaviorTree* Tree, FName Key, float Radius, bool bClear, float Timeout)
		{
			UCSBTTask_MoveToKey* Task = NewObject<UCSBTTask_MoveToKey>(Tree);
			Task->Configure(Key, Radius, bClear, Timeout);
			return Task;
		}
	}

	UBehaviorTree* BuildTree(UObject* Outer, UBlackboardData*& OutBlackboard)
	{
		UBehaviorTree* Tree = NewObject<UBehaviorTree>(Outer, TEXT("BT_CSBot"));

		// Blackboard. SelfActor is added automatically by UBlackboardData.
		UBlackboardData* BB = NewObject<UBlackboardData>(Tree, TEXT("BB_CSBot"));
		AddObjectKey(BB, KeyTarget);
		AddVectorKey(BB, KeyLastKnownLocation);
		AddObjectKey(BB, KeyLootTarget);
		AddVectorKey(BB, KeyPatrolLocation);
		Tree->BlackboardAsset = BB;
		OutBlackboard = BB;

		// Root.
		UBTComposite_Selector* Root = Node<UBTComposite_Selector>(Tree, TEXT("Root"));
		Root->Services.Add(NewObject<UCSBTService_BotBrain>(Tree));
		Tree->RootNode = Root;

		// Fight.
		UBTComposite_Sequence* Fight = Node<UBTComposite_Sequence>(Tree, TEXT("Fight"));
		AddTask(Fight, NewObject<UCSBTTask_Engage>(Tree));
		AddBranch(Root, Fight, KeyIsSet(Tree, KeyTarget));

		// Investigate a sound, a hit or where the target was last seen.
		UBTComposite_Sequence* Investigate = Node<UBTComposite_Sequence>(Tree, TEXT("Investigate"));
		AddTask(Investigate, MoveTo(Tree, KeyLastKnownLocation, 150.f, /*bClear*/ true, 12.f));
		AddBranch(Root, Investigate, KeyIsSet(Tree, KeyLastKnownLocation));

		// Loot.
		UBTComposite_Sequence* Loot = Node<UBTComposite_Sequence>(Tree, TEXT("Loot"));
		AddTask(Loot, MoveTo(Tree, KeyLootTarget, 50.f, /*bClear*/ false, 12.f));
		AddTask(Loot, NewObject<UCSBTTask_PickupLoot>(Tree));
		AddBranch(Root, Loot, KeyIsSet(Tree, KeyLootTarget));

		// Patrol.
		UBTComposite_Sequence* Patrol = Node<UBTComposite_Sequence>(Tree, TEXT("Patrol"));
		AddTask(Patrol, NewObject<UCSBTTask_FindPatrolPoint>(Tree));
		AddTask(Patrol, MoveTo(Tree, KeyPatrolLocation, 120.f, /*bClear*/ false, 15.f));
		UCSBTTask_Pause* Pause = NewObject<UCSBTTask_Pause>(Tree);
		Pause->Configure(0.5f, 1.5f);
		AddTask(Patrol, Pause);
		AddBranch(Root, Patrol, nullptr);

		return Tree;
	}
}
